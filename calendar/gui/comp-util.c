/*
 * Evolution calendar - Utilities for manipulating ECalComponent objects
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Federico Mena-Quintero <federico@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <time.h>

#include "calendar-config.h"
#include "comp-util.h"
#include "dialogs/delete-comp.h"

#include "gnome-cal.h"
#include "shell/e-shell-window.h"
#include "shell/e-shell-view.h"

/**
 * cal_comp_util_add_exdate:
 * @comp: A calendar component object.
 * @itt: Time for the exception.
 *
 * Adds an exception date to the current list of EXDATE properties in a calendar
 * component object.
 **/
void
cal_comp_util_add_exdate (ECalComponent *comp,
                          time_t t,
                          icaltimezone *zone)
{
	GSList *list;
	ECalComponentDateTime *cdt;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	e_cal_component_get_exdate_list (comp, &list);

	cdt = g_new (ECalComponentDateTime, 1);
	cdt->value = g_new (struct icaltimetype, 1);
	*cdt->value = icaltime_from_timet_with_zone (t, FALSE, zone);
	cdt->tzid = g_strdup (icaltimezone_get_tzid (zone));

	list = g_slist_append (list, cdt);
	e_cal_component_set_exdate_list (comp, list);
	e_cal_component_free_exdate_list (list);
}

/* Returns TRUE if the TZIDs are equivalent, i.e. both NULL or the same. */
static gboolean
e_cal_component_compare_tzid (const gchar *tzid1,
                              const gchar *tzid2)
{
	gboolean retval = TRUE;

	if (tzid1) {
		if (!tzid2 || strcmp (tzid1, tzid2))
			retval = FALSE;
	} else {
		if (tzid2)
			retval = FALSE;
	}

	return retval;
}

/**
 * cal_comp_util_compare_event_timezones:
 * @comp: A calendar component object.
 * @client: A #ECalClient.
 *
 * Checks if the component uses the given timezone for both the start and
 * the end time, or if the UTC offsets of the start and end times are the same
 * as in the given zone.
 *
 * Returns: TRUE if the component's start and end time are at the same UTC
 * offset in the given timezone.
 **/
gboolean
cal_comp_util_compare_event_timezones (ECalComponent *comp,
                                       ECalClient *client,
                                       icaltimezone *zone)
{
	ECalComponentDateTime start_datetime, end_datetime;
	const gchar *tzid;
	gboolean retval = FALSE;
	icaltimezone *start_zone = NULL;
	icaltimezone *end_zone = NULL;
	gint offset1, offset2;

	tzid = icaltimezone_get_tzid (zone);

	e_cal_component_get_dtstart (comp, &start_datetime);
	e_cal_component_get_dtend (comp, &end_datetime);

	/* If either the DTSTART or the DTEND is a DATE value, we return TRUE.
	 * Maybe if one was a DATE-TIME we should check that, but that should
	 * not happen often. */
	if ((start_datetime.value && start_datetime.value->is_date)
	    || (end_datetime.value && end_datetime.value->is_date)) {
		retval = TRUE;
		goto out;
	}

	/* If the event uses UTC for DTSTART & DTEND, return TRUE. Outlook
	 * will send single events as UTC, so we don't want to mark all of
	 * these. */
	if ((!start_datetime.value || start_datetime.value->is_utc)
	    && (!end_datetime.value || end_datetime.value->is_utc)) {
		retval = TRUE;
		goto out;
	}

	/* If the event uses floating time for DTSTART & DTEND, return TRUE.
	 * Imported vCalendar files will use floating times, so we don't want
	 * to mark all of these. */
	if (!start_datetime.tzid && !end_datetime.tzid) {
		retval = TRUE;
		goto out;
	}

	/* FIXME: DURATION may be used instead. */
	if (e_cal_component_compare_tzid (tzid, start_datetime.tzid)
	    && e_cal_component_compare_tzid (tzid, end_datetime.tzid)) {
		/* If both TZIDs are the same as the given zone's TZID, then
		 * we know the timezones are the same so we return TRUE. */
		retval = TRUE;
	} else {
		/* If the TZIDs differ, we have to compare the UTC offsets
		 * of the start and end times, using their own timezones and
		 * the given timezone. */
		e_cal_client_get_timezone_sync (
			client, start_datetime.tzid, &start_zone, NULL, NULL);
		if (start_zone == NULL)
			goto out;

		if (start_datetime.value) {
			offset1 = icaltimezone_get_utc_offset (
				start_zone,
				start_datetime.value,
				NULL);
			offset2 = icaltimezone_get_utc_offset (
				zone,
				start_datetime.value,
				NULL);
			if (offset1 != offset2)
				goto out;
		}

		e_cal_client_get_timezone_sync (
			client, end_datetime.tzid, &end_zone, NULL, NULL);
		if (end_zone == NULL)
			goto out;

		if (end_datetime.value) {
			offset1 = icaltimezone_get_utc_offset (
				end_zone,
				end_datetime.value,
				NULL);
			offset2 = icaltimezone_get_utc_offset (
				zone,
				end_datetime.value,
				NULL);
			if (offset1 != offset2)
				goto out;
		}

		retval = TRUE;
	}

 out:

	e_cal_component_free_datetime (&start_datetime);
	e_cal_component_free_datetime (&end_datetime);

	return retval;
}

/**
 * cal_comp_confirm_delete_empty_comp:
 * @comp: A calendar component.
 * @client: Calendar client where the component purportedly lives.
 * @widget: Widget to be used as the basis for UTF8 conversion.
 *
 * Assumming a calendar component with an empty SUMMARY property (as per
 * string_is_empty()), asks whether the user wants to delete it based on
 * whether the appointment is on the calendar server or not.  If the
 * component is on the server, this function will present a confirmation
 * dialog and delete the component if the user tells it to.  If the component
 * is not on the server it will just return TRUE.
 *
 * Return value: A result code indicating whether the component
 * was not on the server and is to be deleted locally, whether it
 * was on the server and the user deleted it, or whether the
 * user cancelled the deletion.
 **/
gboolean
cal_comp_is_on_server (ECalComponent *comp,
                       ECalClient *client)
{
	const gchar *uid;
	gchar *rid = NULL;
	icalcomponent *icalcomp = NULL;
	GError *error = NULL;

	g_return_val_if_fail (comp != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	/* See if the component is on the server.  If it is not, then it likely
	 * means that the appointment is new, only in the day view, and we
	 * haven't added it yet to the server.  In that case, we don't need to
	 * confirm and we can just delete the event.  Otherwise, we ask
	 * the user.
	 */
	e_cal_component_get_uid (comp, &uid);

	/* TODO We should not be checking for this here. But since
	 *      e_cal_util_construct_instance does not create the instances
	 *      of all day events, so we default to old behaviour. */
	if (e_cal_client_check_recurrences_no_master (client)) {
		rid = e_cal_component_get_recurid_as_string (comp);
	}

	e_cal_client_get_object_sync (
		client, uid, rid, &icalcomp, NULL, &error);

	if (icalcomp != NULL) {
		icalcomponent_free (icalcomp);
		g_free (rid);

		return TRUE;
	}

	if (!g_error_matches (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND))
		g_warning (G_STRLOC ": %s", error->message);

	g_clear_error (&error);
	g_free (rid);

	return FALSE;
}

/**
 * is_icalcomp_on_the_server:
 * same as @cal_comp_is_on_server, only the component parameter is
 * icalcomponent, not the ECalComponent.
 **/
gboolean
is_icalcomp_on_the_server (icalcomponent *icalcomp,
                           ECalClient *client)
{
	gboolean on_server;
	ECalComponent *comp;

	if (!icalcomp || !client || !icalcomponent_get_uid (icalcomp))
		return FALSE;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));

	on_server = cal_comp_is_on_server (comp, client);

	g_object_unref (comp);

	return on_server;
}

/**
 * cal_comp_event_new_with_defaults:
 *
 * Creates a new VEVENT component and adds any default alarms to it as set in
 * the program's configuration values, but only if not the all_day event.
 *
 * Return value: A newly-created calendar component.
 **/
ECalComponent *
cal_comp_event_new_with_defaults (ECalClient *client,
                                  gboolean all_day,
                                  gboolean use_default_reminder,
                                  gint default_reminder_interval,
                                  EDurationType default_reminder_units)
{
	icalcomponent *icalcomp = NULL;
	ECalComponent *comp;
	ECalComponentAlarm *alarm;
	icalproperty *icalprop;
	ECalComponentAlarmTrigger trigger;

	e_cal_client_get_default_object_sync (client, &icalcomp, NULL, NULL);
	if (icalcomp == NULL)
		icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);

	comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
		icalcomponent_free (icalcomp);

		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
	}

	if (all_day || !use_default_reminder)
		return comp;

	alarm = e_cal_component_alarm_new ();

	/* We don't set the description of the alarm; we'll copy it from the
	 * summary when it gets committed to the server. For that, we add a
	 * X-EVOLUTION-NEEDS-DESCRIPTION property to the alarm's component.
	 */
	icalcomp = e_cal_component_alarm_get_icalcomponent (alarm);
	icalprop = icalproperty_new_x ("1");
	icalproperty_set_x_name (icalprop, "X-EVOLUTION-NEEDS-DESCRIPTION");
	icalcomponent_add_property (icalcomp, icalprop);

	e_cal_component_alarm_set_action (alarm, E_CAL_COMPONENT_ALARM_DISPLAY);

	trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;

	memset (&trigger.u.rel_duration, 0, sizeof (trigger.u.rel_duration));

	trigger.u.rel_duration.is_neg = TRUE;

	switch (default_reminder_units) {
	case E_DURATION_MINUTES:
		trigger.u.rel_duration.minutes = default_reminder_interval;
		break;

	case E_DURATION_HOURS:
		trigger.u.rel_duration.hours = default_reminder_interval;
		break;

	case E_DURATION_DAYS:
		trigger.u.rel_duration.days = default_reminder_interval;
		break;

	default:
		g_warning ("wrong units %d\n", default_reminder_units);
	}

	e_cal_component_alarm_set_trigger (alarm, trigger);

	e_cal_component_add_alarm (comp, alarm);
	e_cal_component_alarm_free (alarm);

	return comp;
}

ECalComponent *
cal_comp_event_new_with_current_time (ECalClient *client,
                                      gboolean all_day,
                                      gboolean use_default_reminder,
                                      gint default_reminder_interval,
                                      EDurationType default_reminder_units)
{
	ECalComponent *comp;
	struct icaltimetype itt;
	ECalComponentDateTime dt;
	icaltimezone *zone;

	comp = cal_comp_event_new_with_defaults (
		client, all_day, use_default_reminder,
		default_reminder_interval, default_reminder_units);
	g_return_val_if_fail (comp != NULL, NULL);

	zone = e_cal_client_get_default_timezone (client);

	if (all_day) {
		itt = icaltime_from_timet_with_zone (time (NULL), 1, zone);

		dt.value = &itt;
		dt.tzid = icaltimezone_get_tzid (zone);

		e_cal_component_set_dtstart (comp, &dt);
		e_cal_component_set_dtend (comp, &dt);
	} else {
		itt = icaltime_current_time_with_zone (zone);
		icaltime_adjust (&itt, 0, 1, -itt.minute, -itt.second);

		dt.value = &itt;
		dt.tzid = icaltimezone_get_tzid (zone);

		e_cal_component_set_dtstart (comp, &dt);
		icaltime_adjust (&itt, 0, 1, 0, 0);
		e_cal_component_set_dtend (comp, &dt);
	}

	return comp;
}

ECalComponent *
cal_comp_task_new_with_defaults (ECalClient *client)
{
	ECalComponent *comp;
	icalcomponent *icalcomp = NULL;

	e_cal_client_get_default_object_sync (client, &icalcomp, NULL, NULL);
	if (icalcomp == NULL)
		icalcomp = icalcomponent_new (ICAL_VTODO_COMPONENT);

	comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
		icalcomponent_free (icalcomp);

		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
	}

	return comp;
}

ECalComponent *
cal_comp_memo_new_with_defaults (ECalClient *client)
{
	ECalComponent *comp;
	icalcomponent *icalcomp = NULL;

	e_cal_client_get_default_object_sync (client, &icalcomp, NULL, NULL);
	if (icalcomp == NULL)
		icalcomp = icalcomponent_new (ICAL_VJOURNAL_COMPONENT);

	comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
		icalcomponent_free (icalcomp);

		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_JOURNAL);
	}

	return comp;
}

void
cal_comp_update_time_by_active_window (ECalComponent *comp,
                                       EShell *shell)
{
	GtkWindow *window;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (shell != NULL);

	window = e_shell_get_active_window (shell);

	if (E_IS_SHELL_WINDOW (window)) {
		EShellWindow *shell_window;
		const gchar *active_view;

		shell_window = E_SHELL_WINDOW (window);
		active_view = e_shell_window_get_active_view (shell_window);

		if (g_strcmp0 (active_view, "calendar") == 0) {
			EShellContent *shell_content;
			EShellView *shell_view;
			GnomeCalendar *gnome_cal;
			time_t start = 0, end = 0;
			icaltimezone *zone;
			struct icaltimetype itt;
			icalcomponent *icalcomp;
			icalproperty *prop;

			shell_view = e_shell_window_peek_shell_view (
				shell_window, "calendar");
			g_return_if_fail (shell_view != NULL);

			gnome_cal = NULL;
			shell_content = e_shell_view_get_shell_content (shell_view);
			g_object_get (shell_content, "calendar", &gnome_cal, NULL);
			g_return_if_fail (gnome_cal != NULL);

			gnome_calendar_get_current_time_range (gnome_cal, &start, &end);
			g_return_if_fail (start != 0);

			zone = e_cal_model_get_timezone (gnome_calendar_get_model (gnome_cal));
			itt = icaltime_from_timet_with_zone (start, FALSE, zone);

			icalcomp = e_cal_component_get_icalcomponent (comp);
			prop = icalcomponent_get_first_property (icalcomp, ICAL_DTSTART_PROPERTY);
			if (prop) {
				icalproperty_set_dtstart (prop, itt);
			} else {
				prop = icalproperty_new_dtstart (itt);
				icalcomponent_add_property (icalcomp, prop);
			}

			e_cal_component_rescan (comp);
		}
	}
}

/**
 * cal_comp_util_get_n_icons:
 * @comp: A calendar component object.
 * @pixbufs: List of pixbufs to use. Can be NULL.
 *
 * Get the number of icons owned by the component.
 * Each member of pixmaps should be freed with g_object_unref
 * and the list itself should be freed too.
 *
 * Returns: the number of icons owned by the component.
 **/
gint
cal_comp_util_get_n_icons (ECalComponent *comp,
                           GSList **pixbufs)
{
	GSList *categories_list, *elem;
	gint num_icons = 0;

	g_return_val_if_fail (comp != NULL, 0);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), 0);

	e_cal_component_get_categories_list (comp, &categories_list);
	for (elem = categories_list; elem; elem = elem->next) {
		const gchar *category;
		GdkPixbuf *pixbuf = NULL;

		category = elem->data;
		if (e_categories_config_get_icon_for (category, &pixbuf)) {
			if (!pixbuf)
				continue;

			num_icons++;

			if (pixbufs) {
				*pixbufs = g_slist_append (*pixbufs, pixbuf);
			} else {
				g_object_unref (pixbuf);
			}
		}
	}
	e_cal_component_free_categories_list (categories_list);

	return num_icons;
}

/**
 * cal_comp_selection_set_string_list
 * @data: Selection data, where to put list of strings.
 * @str_list: List of strings. (Each element is of type const gchar *.)
 *
 * Stores list of strings into selection target data.  Use
 * cal_comp_selection_get_string_list() to get this list from target data.
 **/
void
cal_comp_selection_set_string_list (GtkSelectionData *data,
                                    GSList *str_list)
{
	/* format is "str1\0str2\0...strN\0" */
	GSList *p;
	GByteArray *array;
	GdkAtom target;

	g_return_if_fail (data != NULL);

	if (!str_list)
		return;

	array = g_byte_array_new ();
	for (p = str_list; p; p = p->next) {
		const guint8 *c = p->data;

		if (c)
			g_byte_array_append (array, c, strlen ((const gchar *) c) + 1);
	}

	target = gtk_selection_data_get_target (data);
	gtk_selection_data_set (data, target, 8, array->data, array->len);
	g_byte_array_free (array, TRUE);
}

/**
 * cal_comp_selection_get_string_list
 * @data: Selection data, where to put list of strings.
 *
 * Converts data from selection to list of strings. Data should be assigned
 * to selection data with cal_comp_selection_set_string_list().
 * Each string in newly created list should be freed by g_free().
 * List itself should be freed by g_slist_free().
 *
 * Returns: Newly allocated #GSList of strings.
 **/
GSList *
cal_comp_selection_get_string_list (GtkSelectionData *selection_data)
{
	/* format is "str1\0str2\0...strN\0" */
	gchar *inptr, *inend;
	GSList *list;
	const guchar *data;
	gint length;

	g_return_val_if_fail (selection_data != NULL, NULL);

	data = gtk_selection_data_get_data (selection_data);
	length = gtk_selection_data_get_length (selection_data);

	list = NULL;
	inptr = (gchar *) data;
	inend = (gchar *) (data + length);

	while (inptr < inend) {
		gchar *start = inptr;

		while (inptr < inend && *inptr)
			inptr++;

		list = g_slist_prepend (list, g_strndup (start, inptr - start));

		inptr++;
	}

	return list;
}

static void
datetime_to_zone (ECalClient *client,
                  ECalComponentDateTime *date,
                  const gchar *tzid)
{
	icaltimezone *from, *to;

	g_return_if_fail (date != NULL);

	if (date->tzid == NULL || tzid == NULL ||
	    date->tzid == tzid || g_str_equal (date->tzid, tzid))
		return;

	from = icaltimezone_get_builtin_timezone_from_tzid (date->tzid);
	if (!from) {
		GError *error = NULL;

		e_cal_client_get_timezone_sync (
			client, date->tzid, &from, NULL, &error);

		if (error != NULL) {
			g_warning (
				"%s: Could not get timezone '%s' from server: %s",
				G_STRFUNC, date->tzid ? date->tzid : "",
				error->message);
			g_error_free (error);
		}
	}

	to = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (!to) {
		/* do not check failure here, maybe the zone is not available there */
		e_cal_client_get_timezone_sync (client, tzid, &to, NULL, NULL);
	}

	icaltimezone_convert_time (date->value, from, to);
	date->tzid = tzid;
}

/**
 * cal_comp_set_dtstart_with_oldzone:
 * @client: ECalClient structure, to retrieve timezone from, when required.
 * @comp: Component, where make the change.
 * @pdate: Value, to change to.
 *
 * Changes 'dtstart' of the component, but converts time to the old timezone.
 **/
void
cal_comp_set_dtstart_with_oldzone (ECalClient *client,
                                   ECalComponent *comp,
                                   const ECalComponentDateTime *pdate)
{
	ECalComponentDateTime olddate, date;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (pdate != NULL);

	e_cal_component_get_dtstart (comp, &olddate);

	date = *pdate;

	datetime_to_zone (client, &date, olddate.tzid);
	e_cal_component_set_dtstart (comp, &date);

	e_cal_component_free_datetime (&olddate);
}

/**
 * cal_comp_set_dtend_with_oldzone:
 * @client: ECalClient structure, to retrieve timezone from, when required.
 * @comp: Component, where make the change.
 * @pdate: Value, to change to.
 *
 * Changes 'dtend' of the component, but converts time to the old timezone.
 **/
void
cal_comp_set_dtend_with_oldzone (ECalClient *client,
                                 ECalComponent *comp,
                                 const ECalComponentDateTime *pdate)
{
	ECalComponentDateTime olddate, date;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (pdate != NULL);

	e_cal_component_get_dtend (comp, &olddate);

	date = *pdate;

	datetime_to_zone (client, &date, olddate.tzid);
	e_cal_component_set_dtend (comp, &date);

	e_cal_component_free_datetime (&olddate);
}

void
comp_util_sanitize_recurrence_master (ECalComponent *comp,
                                      ECalClient *client)
{
	ECalComponent *master = NULL;
	icalcomponent *icalcomp = NULL;
	ECalComponentRange rid;
	ECalComponentDateTime sdt;
	const gchar *uid;
	GError *error = NULL;

	/* Get the master component */
	e_cal_component_get_uid (comp, &uid);

	e_cal_client_get_object_sync (
		client, uid, NULL, &icalcomp, NULL, &error);

	if (error != NULL) {
		g_warning (
			"Unable to get the master component: %s",
			error->message);
		g_error_free (error);
		return;
	}

	master = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (master, icalcomp)) {
		icalcomponent_free (icalcomp);
		g_object_unref (master);
		g_return_if_reached ();
		return;
	}

	/* Compare recur id and start date */
	e_cal_component_get_recurid (comp, &rid);
	e_cal_component_get_dtstart (comp, &sdt);

	if (rid.datetime.value && sdt.value &&
	    icaltime_compare_date_only (
	    *rid.datetime.value, *sdt.value) == 0) {
		ECalComponentDateTime msdt, medt, edt;
		gint *sequence;

		e_cal_component_get_dtstart (master, &msdt);
		e_cal_component_get_dtend (master, &medt);

		e_cal_component_get_dtend (comp, &edt);

		g_return_if_fail (msdt.value != NULL);
		g_return_if_fail (medt.value != NULL);
		g_return_if_fail (edt.value != NULL);

		sdt.value->year = msdt.value->year;
		sdt.value->month = msdt.value->month;
		sdt.value->day = msdt.value->day;

		edt.value->year = medt.value->year;
		edt.value->month = medt.value->month;
		edt.value->day = medt.value->day;

		e_cal_component_set_dtstart (comp, &sdt);
		e_cal_component_set_dtend (comp, &edt);

		e_cal_component_get_sequence (master, &sequence);
		e_cal_component_set_sequence (comp, sequence);

		e_cal_component_free_datetime (&msdt);
		e_cal_component_free_datetime (&medt);
		e_cal_component_free_datetime (&edt);
	}

	e_cal_component_free_datetime (&sdt);
	e_cal_component_free_range (&rid);
	e_cal_component_set_recurid (comp, NULL);

	g_object_unref (master);
}

gchar *
icalcomp_suggest_filename (icalcomponent *icalcomp,
                           const gchar *default_name)
{
	icalproperty *prop;
	const gchar *summary = NULL;

	if (!icalcomp)
		return g_strconcat (default_name, ".ics", NULL);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_SUMMARY_PROPERTY);
	if (prop)
		summary = icalproperty_get_summary (prop);

	if (!summary || !*summary)
		summary = default_name;

	return g_strconcat (summary, ".ics", NULL);
}

typedef struct _AsyncContext {
	ECalClient *src_client;
	icalcomponent *icalcomp_clone;
	gboolean do_copy;
} AsyncContext;

struct ForeachTzidData
{
	ECalClient *source_client;
	ECalClient *destination_client;
	GCancellable *cancellable;
	GError **error;
	gboolean success;
};

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->src_client)
		g_object_unref (async_context->src_client);

	if (async_context->icalcomp_clone)
		icalcomponent_free (async_context->icalcomp_clone);

	g_slice_free (AsyncContext, async_context);
}

static void
add_timezone_to_cal_cb (icalparameter *param,
                        gpointer data)
{
	struct ForeachTzidData *ftd = data;
	icaltimezone *tz = NULL;
	const gchar *tzid;

	g_return_if_fail (ftd != NULL);
	g_return_if_fail (ftd->source_client != NULL);
	g_return_if_fail (ftd->destination_client != NULL);

	if (!ftd->success)
		return;

	if (ftd->cancellable && g_cancellable_is_cancelled (ftd->cancellable)) {
		ftd->success = FALSE;
		return;
	}

	tzid = icalparameter_get_tzid (param);
	if (!tzid || !*tzid)
		return;

	if (e_cal_client_get_timezone_sync (ftd->source_client, tzid, &tz, ftd->cancellable, NULL) && tz)
		ftd->success = e_cal_client_add_timezone_sync (
				ftd->destination_client, tz, ftd->cancellable, ftd->error);
}

/* Helper for cal_comp_transfer_item_to() */
static void
cal_comp_transfer_item_to_thread (GSimpleAsyncResult *simple,
                                  GObject *source_object,
                                  GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	cal_comp_transfer_item_to_sync (
		async_context->src_client,
		E_CAL_CLIENT (source_object),
		async_context->icalcomp_clone,
		async_context->do_copy,
		cancellable, &local_error);

	if (local_error != NULL)
		g_simple_async_result_take_error (simple, local_error);
}

void
cal_comp_transfer_item_to (ECalClient *src_client,
                           ECalClient *dest_client,
                           icalcomponent *icalcomp_vcal,
                           gboolean do_copy,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (src_client));
	g_return_if_fail (E_IS_CAL_CLIENT (dest_client));
	g_return_if_fail (icalcomp_vcal != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->src_client = g_object_ref (src_client);
	async_context->icalcomp_clone = icalcomponent_new_clone (icalcomp_vcal);
	async_context->do_copy = do_copy;

	simple = g_simple_async_result_new (
		G_OBJECT (dest_client), callback, user_data,
		cal_comp_transfer_item_to);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, cal_comp_transfer_item_to_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

gboolean
cal_comp_transfer_item_to_finish (ECalClient *client,
                                  GAsyncResult *result,
                                  GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (result, G_OBJECT (client), cal_comp_transfer_item_to),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return TRUE;
}

gboolean
cal_comp_transfer_item_to_sync (ECalClient *src_client,
                                ECalClient *dest_client,
                                icalcomponent *icalcomp_vcal,
                                gboolean do_copy,
                                GCancellable *cancellable,
                                GError **error)
{
	icalcomponent *icalcomp;
	icalcomponent *icalcomp_event, *subcomp;
	icalcomponent_kind icalcomp_kind;
	const gchar *uid;
	gchar *new_uid = NULL;
	struct ForeachTzidData ftd;
	ECalClientSourceType source_type;
	GHashTable *processed_uids;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_CLIENT (src_client), FALSE);
	g_return_val_if_fail (E_IS_CAL_CLIENT (dest_client), FALSE);
	g_return_val_if_fail (icalcomp_vcal != NULL, FALSE);

	icalcomp_event = icalcomponent_get_inner (icalcomp_vcal);
	g_return_val_if_fail (icalcomp_event != NULL, FALSE);

	source_type = e_cal_client_get_source_type (src_client);
	switch (source_type) {
		case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
			icalcomp_kind = ICAL_VEVENT_COMPONENT;
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
			icalcomp_kind = ICAL_VTODO_COMPONENT;
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
			icalcomp_kind = ICAL_VJOURNAL_COMPONENT;
			break;
		default:
			g_return_val_if_reached (FALSE);
	}

	processed_uids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	icalcomp_event = icalcomponent_get_first_component (icalcomp_vcal, icalcomp_kind);
	/*
	 * This check should be removed in the near future.
	 * We should be able to work properly with multiselection, which means that we always
	 * will receive a component with subcomponents.
	 */
	if (icalcomp_event == NULL)
		icalcomp_event = icalcomp_vcal;
	for (;
	     icalcomp_event;
	     icalcomp_event = icalcomponent_get_next_component (icalcomp_vcal, icalcomp_kind)) {
		GError *local_error = NULL;

		uid = icalcomponent_get_uid (icalcomp_event);

		if (g_hash_table_lookup (processed_uids, uid))
			continue;

		success = e_cal_client_get_object_sync (dest_client, uid, NULL, &icalcomp, cancellable, &local_error);
		if (success) {
			success = e_cal_client_modify_object_sync (
				dest_client, icalcomp_event, CALOBJ_MOD_ALL, cancellable, error);

			icalcomponent_free (icalcomp);
			if (!success)
				goto exit;

			if (!do_copy) {
				ECalObjModType mod_type = CALOBJ_MOD_THIS;

				/* Remove the item from the source calendar. */
				if (e_cal_util_component_is_instance (icalcomp_event) ||
				    e_cal_util_component_has_recurrences (icalcomp_event))
					mod_type = CALOBJ_MOD_ALL;

				success = e_cal_client_remove_object_sync (
						src_client, uid, NULL, mod_type, cancellable, error);
				if (!success)
					goto exit;
			}

			continue;
		} else if (local_error != NULL && !g_error_matches (
					local_error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND)) {
			g_propagate_error (error, local_error);
			goto exit;
		} else {
			g_clear_error (&local_error);
		}

		if (e_cal_util_component_is_instance (icalcomp_event)) {
			GSList *ecalcomps = NULL, *eiter;
			ECalComponent *comp;

			success = e_cal_client_get_objects_for_uid_sync (src_client, uid, &ecalcomps, cancellable, error);
			if (!success)
				goto exit;

			if (ecalcomps && !ecalcomps->next) {
				/* only one component, no need for a vCalendar list */
				comp = ecalcomps->data;
				icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));
			} else {
				icalcomp = icalcomponent_new (ICAL_VCALENDAR_COMPONENT);
				for (eiter = ecalcomps; eiter; eiter = g_slist_next (eiter)) {
					comp = eiter->data;

					icalcomponent_add_component (
						icalcomp,
						icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));
				}
			}

			e_cal_client_free_ecalcomp_slist (ecalcomps);
		} else {
			icalcomp = icalcomponent_new_clone (icalcomp_event);
		}

		if (do_copy) {
			/* Change the UID to avoid problems with duplicated UID */
			new_uid = e_cal_component_gen_uid ();
			if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
				/* in case of a vCalendar, the component might have detached instances,
				 * thus change the UID on all of the subcomponents of it */
				for (subcomp = icalcomponent_get_first_component (icalcomp, icalcomp_kind);
				     subcomp;
				     subcomp = icalcomponent_get_next_component (icalcomp, icalcomp_kind)) {
					icalcomponent_set_uid (subcomp, new_uid);
				}
			} else {
				icalcomponent_set_uid (icalcomp, new_uid);
			}
			g_free (new_uid);
			new_uid = NULL;
		}

		ftd.source_client = src_client;
		ftd.destination_client = dest_client;
		ftd.cancellable = cancellable;
		ftd.error = error;
		ftd.success = TRUE;

		if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
			/* in case of a vCalendar, the component might have detached instances,
			 * thus check timezones on all of the subcomponents of it */
			for (subcomp = icalcomponent_get_first_component (icalcomp, icalcomp_kind);
			     subcomp && ftd.success;
			     subcomp = icalcomponent_get_next_component (icalcomp, icalcomp_kind)) {
				icalcomponent_foreach_tzid (subcomp, add_timezone_to_cal_cb, &ftd);
			}
		} else {
			icalcomponent_foreach_tzid (icalcomp, add_timezone_to_cal_cb, &ftd);
		}

		if (!ftd.success) {
			success = FALSE;
			goto exit;
		}

		if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
			gboolean did_add = FALSE;

			/* in case of a vCalendar, the component might have detached instances,
			 * thus add the master object first, and then all of the subcomponents of it */
			for (subcomp = icalcomponent_get_first_component (icalcomp, icalcomp_kind);
			     subcomp && !did_add;
			     subcomp = icalcomponent_get_next_component (icalcomp, icalcomp_kind)) {
				if (icaltime_is_null_time (icalcomponent_get_recurrenceid (subcomp))) {
					did_add = TRUE;
					success = e_cal_client_create_object_sync (
						dest_client, subcomp,
						&new_uid, cancellable, error);
					g_free (new_uid);
				}
			}

			if (!success) {
				icalcomponent_free (icalcomp);
				goto exit;
			}

			/* deal with detached instances */
			for (subcomp = icalcomponent_get_first_component (icalcomp, icalcomp_kind);
			     subcomp && success;
			     subcomp = icalcomponent_get_next_component (icalcomp, icalcomp_kind)) {
				if (!icaltime_is_null_time (icalcomponent_get_recurrenceid (subcomp))) {
					if (did_add) {
						success = e_cal_client_modify_object_sync (
							dest_client, subcomp,
							CALOBJ_MOD_THIS, cancellable, error);
					} else {
						/* just in case there are only detached instances and no master object */
						did_add = TRUE;
						success = e_cal_client_create_object_sync (
							dest_client, subcomp,
							&new_uid, cancellable, error);
						g_free (new_uid);
					}
				}
			}
		} else {
			success = e_cal_client_create_object_sync (dest_client, icalcomp, &new_uid, cancellable, error);
			g_free (new_uid);
		}

		icalcomponent_free (icalcomp);
		if (!success)
			goto exit;

		if (!do_copy) {
			ECalObjModType mod_type = CALOBJ_MOD_THIS;

			/* Remove the item from the source calendar. */
			if (e_cal_util_component_is_instance (icalcomp_event) ||
			    e_cal_util_component_has_recurrences (icalcomp_event))
				mod_type = CALOBJ_MOD_ALL;

			success = e_cal_client_remove_object_sync (src_client, uid, NULL, mod_type, cancellable, error);
			if (!success)
				goto exit;
		}

		g_hash_table_insert (processed_uids, g_strdup (uid), GINT_TO_POINTER (1));
	}

 exit:
	g_hash_table_destroy (processed_uids);

	return success;
}
