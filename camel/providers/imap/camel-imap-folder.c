/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.c: Abstract class for an imap folder */

/* 
 * Authors: Jeffrey Stedfast <fejj@helixcode.com> 
 *
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#include <config.h> 

#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>

#include <gal/util/e-util.h>

#include "camel-imap-folder.h"
#include "camel-imap-command.h"
#include "camel-imap-store.h"
#include "camel-imap-stream.h"
#include "camel-imap-summary.h"
#include "camel-imap-utils.h"
#include "string-utils.h"
#include "camel-stream.h"
#include "camel-stream-fs.h"
#include "camel-stream-mem.h"
#include "camel-stream-buffer.h"
#include "camel-data-wrapper.h"
#include "camel-mime-message.h"
#include "camel-stream-filter.h"
#include "camel-mime-filter-from.h"
#include "camel-mime-filter-crlf.h"
#include "camel-exception.h"
#include "camel-mime-utils.h"

#define d(x) x

#define CF_CLASS(o) (CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(o)))

static CamelFolderClass *parent_class = NULL;

static void imap_finalize (CamelObject *object);
static void imap_refresh_info (CamelFolder *folder, CamelException *ex);
static void imap_sync (CamelFolder *folder, gboolean expunge, CamelException *ex);
static const char *imap_get_full_name (CamelFolder *folder);
static void imap_expunge (CamelFolder *folder, CamelException *ex);

/* message counts */
static gint imap_get_message_count (CamelFolder *folder);
static gint imap_get_unread_message_count (CamelFolder *folder);

/* message manipulation */
static CamelMimeMessage *imap_get_message (CamelFolder *folder, const gchar *uid,
					   CamelException *ex);
static void imap_append_message (CamelFolder *folder, CamelMimeMessage *message,
				 const CamelMessageInfo *info, CamelException *ex);
static void imap_copy_message_to (CamelFolder *source, const char *uid,
				  CamelFolder *destination, CamelException *ex);
static void imap_move_message_to (CamelFolder *source, const char *uid,
				  CamelFolder *destination, CamelException *ex);

/* summary info */
static GPtrArray *imap_get_uids (CamelFolder *folder);
static GPtrArray *imap_get_summary (CamelFolder *folder);
static const CamelMessageInfo *imap_get_message_info (CamelFolder *folder, const char *uid);

static void imap_update_summary (CamelFolder *folder, int first, int last,
				 CamelFolderChangeInfo *changes,
				 CamelException *ex);

/* searching */
static GPtrArray *imap_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex);

/* flag methods */
static guint32  imap_get_message_flags     (CamelFolder *folder, const char *uid);
static void     imap_set_message_flags     (CamelFolder *folder, const char *uid, guint32 flags, guint32 set);
static gboolean imap_get_message_user_flag (CamelFolder *folder, const char *uid, const char *name);
static void     imap_set_message_user_flag (CamelFolder *folder, const char *uid, const char *name,
					    gboolean value);


static void
camel_imap_folder_class_init (CamelImapFolderClass *camel_imap_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_imap_folder_class);

	parent_class = CAMEL_FOLDER_CLASS(camel_type_get_global_classfuncs (camel_folder_get_type ()));
	
	/* virtual method definition */
	
	/* virtual method overload */
	camel_folder_class->refresh_info = imap_refresh_info;
	camel_folder_class->sync = imap_sync;
	camel_folder_class->expunge = imap_expunge;
	camel_folder_class->get_full_name = imap_get_full_name;
	
	camel_folder_class->get_uids = imap_get_uids;
	camel_folder_class->free_uids = camel_folder_free_nop;
	
	camel_folder_class->get_message_count = imap_get_message_count;
	camel_folder_class->get_unread_message_count = imap_get_unread_message_count;
	camel_folder_class->get_message = imap_get_message;
	camel_folder_class->append_message = imap_append_message;
	camel_folder_class->copy_message_to = imap_copy_message_to;
	camel_folder_class->move_message_to = imap_move_message_to;
	
	camel_folder_class->get_summary = imap_get_summary;
	camel_folder_class->get_message_info = imap_get_message_info;
	camel_folder_class->free_summary = camel_folder_free_nop;
	
	camel_folder_class->search_by_expression = imap_search_by_expression;
	
	camel_folder_class->get_message_flags = imap_get_message_flags;
	camel_folder_class->set_message_flags = imap_set_message_flags;
	camel_folder_class->get_message_user_flag = imap_get_message_user_flag;
	camel_folder_class->set_message_user_flag = imap_set_message_user_flag;
}

static void
camel_imap_folder_init (gpointer object, gpointer klass)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (object);
	CamelFolder *folder = CAMEL_FOLDER (object);
	
	folder->has_summary_capability = TRUE;
	folder->has_search_capability = TRUE;
	
	imap_folder->summary = NULL;
}

CamelType
camel_imap_folder_get_type (void)
{
	static CamelType camel_imap_folder_type = CAMEL_INVALID_TYPE;
	
	if (camel_imap_folder_type == CAMEL_INVALID_TYPE) {
		camel_imap_folder_type =
			camel_type_register (CAMEL_FOLDER_TYPE, "CamelImapFolder",
					     sizeof (CamelImapFolder),
					     sizeof (CamelImapFolderClass),
					     (CamelObjectClassInitFunc) camel_imap_folder_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_imap_folder_init,
					     (CamelObjectFinalizeFunc) imap_finalize);
	}
	
	return camel_imap_folder_type;
}

CamelFolder *
camel_imap_folder_new (CamelStore *parent, const char *folder_name,
		       const char *short_name, const char *summary_file,
		       CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (parent);
	CamelFolder *folder = CAMEL_FOLDER (camel_object_new (camel_imap_folder_get_type ()));
	CamelImapFolder *imap_folder = (CamelImapFolder *)folder;
	CamelImapResponse *response;
	const char *resp;
	guint32 validity = 0;
	int i;

	camel_folder_construct (folder, parent, folder_name, short_name);

	response = camel_imap_command (imap_store, folder, ex, NULL);
	if (!response) {
		camel_object_unref ((CamelObject *)folder);
		return NULL;
	}

	for (i = 0; i < response->untagged->len; i++) {
		resp = response->untagged->pdata[i] + 2;
		if (!g_strncasecmp (resp, "FLAGS ", 6)) {
			folder->permanent_flags =
				imap_parse_flag_list (resp + 6);
		} else if (!g_strncasecmp (resp, "OK [PERMANENTFLAGS ", 19)) {
			folder->permanent_flags =
				imap_parse_flag_list (resp + 19);
		} else if (!g_strncasecmp (resp, "OK [UIDVALIDITY ", 16)) {
			validity = strtoul (resp + 16, NULL, 10);
		} else if (isdigit ((unsigned char)*resp)) {
			unsigned long num = strtoul (resp, (char **)&resp, 10);

			if (!g_strncasecmp (resp, " EXISTS", 7))
				imap_folder->exists = num;
		}
	}
	camel_imap_response_free (response);

	imap_folder->summary = camel_imap_summary_new (summary_file, validity);
	if (!imap_folder->summary) {
		camel_object_unref (CAMEL_OBJECT (folder));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not load summary for %s"),
				      folder_name);
		return NULL;
	}

	imap_refresh_info (folder, ex);
	if (camel_exception_is_set (ex)) {
		camel_object_unref (CAMEL_OBJECT (folder));
		return NULL;
	}

	return folder;
}

static void           
imap_finalize (CamelObject *object)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (object);

	camel_object_unref ((CamelObject *)imap_folder->summary);
}

static void
imap_refresh_info (CamelFolder *folder, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapResponse *response;
	struct {
		char *uid;
		guint32 flags;
	} *new = NULL;
	char *resp, *p;
	const char *uid, *flags;
	int i, seq, summary_len;
	CamelMessageInfo *info;
	CamelImapMessageInfo *iinfo;
	CamelFolderChangeInfo *changes;

	changes = camel_folder_change_info_new ();

	/* Get UIDs and flags of all messages. */
	if (imap_folder->exists) {
		response = camel_imap_command (store, folder, ex,
					       "FETCH 1:%d (UID FLAGS)",
					       imap_folder->exists);
		if (!response) {
			camel_folder_change_info_free (changes);
			return;
		}

		new = g_malloc0 (imap_folder->exists * sizeof (*new));
		for (i = 0; i < response->untagged->len; i++) {
			resp = response->untagged->pdata[i];

			seq = strtoul (resp + 2, &resp, 10);
			if (g_strncasecmp (resp, " FETCH ", 7) != 0)
				continue;

			uid = e_strstrcase (resp, "UID ");
			if (uid) {
				uid += 4;
				strtoul (uid, &p, 10);
				new[seq - 1].uid = g_strndup (uid, p - uid);
			}

			flags = e_strstrcase (resp, "FLAGS ");
			if (flags) {
				flags += 6;
				new[seq - 1].flags = imap_parse_flag_list (flags);
			}
		}
		camel_imap_response_free (response);
	}

	/* If we find a UID in the summary that doesn't correspond to
	 * the UID in the folder, that it means the message was
	 * deleted on the server, so we remove it from the summary.
	 */
	summary_len = camel_folder_summary_count (imap_folder->summary);
	for (i = 0; i < summary_len && i < imap_folder->exists; i++) {
		info = camel_folder_summary_index (imap_folder->summary, i);
		iinfo = (CamelImapMessageInfo *)info;

		/* Shouldn't happen, but... */
		if (!new[i].uid)
			continue;

		if (strcmp (camel_message_info_uid (info), new[i].uid) != 0) {
			camel_folder_change_info_remove_uid (changes, camel_message_info_uid (info));
			camel_folder_summary_remove (imap_folder->summary, info);
			i--;
			summary_len--;
			continue;
		}

		/* Update summary flags */
		if (new[i].flags != iinfo->server_flags) {
			guint32 server_set, server_cleared;

			server_set = new[i].flags & ~iinfo->server_flags;
			server_cleared = iinfo->server_flags & ~new[i].flags;

			info->flags = (info->flags | server_set) & ~server_cleared;
			iinfo->server_flags = new[i].flags;

			camel_folder_change_info_change_uid (changes, new[i].uid);
		}

		g_free (new[i].uid);
	}

	/* Remove any leftover cached summary messages. */
	while (summary_len > i + 1) {
		info = camel_folder_summary_index (imap_folder->summary, --summary_len);
		camel_folder_change_info_remove_uid (changes, camel_message_info_uid (info));
		camel_folder_summary_remove (imap_folder->summary, info);
	}

	/* Add any new folder messages. */
	if (i < imap_folder->exists) {
		/* Fetch full summary for the remaining messages. */
		imap_update_summary (folder, i + 1, imap_folder->exists,
				     changes, ex);

		while (i < imap_folder->exists)
			g_free (new[i++].uid);
	}
	g_free (new);

	if (camel_folder_change_info_changed (changes)) {
		camel_object_trigger_event (CAMEL_OBJECT (folder),
					    "folder_changed", changes);
	}
	camel_folder_change_info_free (changes);
}

static void
imap_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapResponse *response;
	int i, max;

	/* Set the flags on any messages that have changed this session */
	max = camel_folder_summary_count (imap_folder->summary);
	for (i = 0; i < max; i++) {
		CamelMessageInfo *info;

		info = camel_folder_summary_index (imap_folder->summary, i);
		if (info->flags & CAMEL_MESSAGE_FOLDER_FLAGGED) {
			char *flags;

			flags = imap_create_flag_list (info->flags);
			if (flags) {
				response = camel_imap_command (
					store, folder, ex,
					"UID STORE %s FLAGS.SILENT %s",
					camel_message_info_uid(info), flags);
				g_free (flags);
				if (!response)
					return;
				camel_imap_response_free (response);
			}
			info->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
		}
	}

	if (expunge) {
		response = camel_imap_command (store, folder, ex, "EXPUNGE");
		camel_imap_response_free (response);
	}

	camel_folder_summary_save (imap_folder->summary);
}

static void
imap_expunge (CamelFolder *folder, CamelException *ex)
{
	imap_sync (folder, TRUE, ex);
}

static const char *
imap_get_full_name (CamelFolder *folder)
{
	CamelURL *url = ((CamelService *)folder->parent_store)->url;
	int len;

	if (!url->path || !*url->path || !strcmp (url->path, "/"))
		return folder->full_name;
	len = strlen (url->path + 1);
	if (!strncmp (url->path + 1, folder->full_name, len) &&
	    strlen (folder->full_name) > len + 1)
		return folder->full_name + len + 1;
	return folder->full_name;
}	

static gint
imap_get_message_count (CamelFolder *folder)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	
	return camel_folder_summary_count (imap_folder->summary);
}

static gint
imap_get_unread_message_count (CamelFolder *folder)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelMessageInfo *info;
	int i, max, count = 0;

	max = camel_folder_summary_count (imap_folder->summary);
	for (i = 0; i < max; i++) {
		info = camel_folder_summary_index (imap_folder->summary, i);
		if (!(info->flags & CAMEL_MESSAGE_SEEN))
			count++;
	}

	return count;
}

static void
imap_append_message (CamelFolder *folder, CamelMimeMessage *message,
		     const CamelMessageInfo *info, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	CamelStream *memstream;
	CamelMimeFilter *crlf_filter;
	CamelStreamFilter *streamfilter;
	GByteArray *ba;
	char *flagstr, *result;
	
	/* create flag string param */
	if (info && info->flags)
		flagstr = imap_create_flag_list (info->flags);
	else
		flagstr = NULL;

	/* FIXME: We could avoid this if we knew how big the message was. */
	memstream = camel_stream_mem_new ();
	ba = g_byte_array_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (memstream), ba);

	streamfilter = camel_stream_filter_new_with_stream (memstream);
	crlf_filter = camel_mime_filter_crlf_new (
		CAMEL_MIME_FILTER_CRLF_ENCODE,
		CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
	camel_stream_filter_add (streamfilter, crlf_filter);
	camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message),
					    CAMEL_STREAM (streamfilter));
	camel_object_unref (CAMEL_OBJECT (streamfilter));
	camel_object_unref (CAMEL_OBJECT (crlf_filter));
	camel_object_unref (CAMEL_OBJECT (memstream));

	response = camel_imap_command (store, NULL, ex, "APPEND %S%s%s {%d}",
				       folder->full_name, flagstr ? " " : "",
				       flagstr ? flagstr : "", ba->len);
	g_free (flagstr);
	
	if (!response) {
		g_byte_array_free (ba, TRUE);
		return;
	}
	result = camel_imap_response_extract_continuation (response, ex);
	if (!result) {
		g_byte_array_free (ba, TRUE);
		return;
	}
	g_free (result);

	/* send the rest of our data - the mime message */
	g_byte_array_append (ba, "\0", 3);
	response = camel_imap_command_continuation (store, ex, ba->data);
	g_byte_array_free (ba, TRUE);
	if (!response)
		return;
	camel_imap_response_free (response);
}

static void
imap_copy_message_to (CamelFolder *source, const char *uid,
		      CamelFolder *destination, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (source->parent_store);
	CamelImapResponse *response;
	
	response = camel_imap_command (store, source, ex, "UID COPY %s %S",
				       uid, destination->full_name);
	camel_imap_response_free (response);
}

static void
imap_move_message_to (CamelFolder *source, const char *uid,
		      CamelFolder *destination, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (source->parent_store);
	CamelImapResponse *response;

	response = camel_imap_command (store, source, ex, "UID COPY %s %S",
				       uid, destination->full_name);
	camel_imap_response_free (response);

	if (camel_exception_is_set (ex))
		return;

	camel_folder_delete_message (source, uid);
}

static GPtrArray *
imap_get_uids (CamelFolder *folder) 
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	const CamelMessageInfo *info;
	GPtrArray *array;
	int i, count;

	count = camel_folder_summary_count (imap_folder->summary);

	array = g_ptr_array_new ();
	g_ptr_array_set_size (array, count);

	for (i = 0; i < count; i++) {
		info = camel_folder_summary_index (imap_folder->summary, i);
		array->pdata[i] = g_strdup (camel_message_info_uid(info));
	}

	return array;
}

static CamelMimeMessage *
imap_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	CamelStream *msgstream;
	CamelMimeMessage *msg;
	char *result, *mesg, *p;
	int len;

	response = camel_imap_command (store, folder, ex,
				       "UID FETCH %s BODY.PEEK[]", uid);
	if (!response)
		return NULL;
	result = camel_imap_response_extract (response, "FETCH", ex);
	if (!result)
		return NULL;

	p = strstr (result, "BODY[]");
	if (p) {
		p += 7;
		mesg = imap_parse_nstring (&p, &len);
	}
	if (!p) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not find message body in FETCH "
					"response."));
		g_free (result);
		return NULL;
	}
	g_free (result);

	msgstream = camel_stream_mem_new_with_buffer (mesg, len);
	msg = camel_mime_message_new ();
	camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg),
						  msgstream);
	camel_object_unref (CAMEL_OBJECT (msgstream));
	g_free (mesg);

	return msg;
}

/**
 * imap_protocol_get_summary_specifier
 *
 * Make a data item specifier for the header lines we need,
 * appropriate to the server level.
 **/
static char *
imap_protocol_get_summary_specifier (CamelImapStore *store)
{
	char *sect_begin, *sect_end;
	char *headers_wanted = "SUBJECT FROM TO CC DATE MESSAGE-ID REFERENCES IN-REPLY-TO";

	if (store->server_level >= IMAP_LEVEL_IMAP4REV1) {
		sect_begin = "BODY.PEEK[HEADER.FIELDS";
		sect_end = "]";
	} else {
		sect_begin = "RFC822.HEADER.LINES";
		sect_end   = "";
	}

	return g_strdup_printf ("UID FLAGS RFC822.SIZE %s (%s)%s", sect_begin,
				headers_wanted, sect_end);
}

static void
imap_update_summary (CamelFolder *folder, int first, int last,
		     CamelFolderChangeInfo *changes, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapResponse *response;
	GPtrArray *headers = NULL;
	char *q, *summary_specifier;
	struct _header_raw *h = NULL;
	int i;

	summary_specifier = imap_protocol_get_summary_specifier (store);
	if (first == last) {
		response = camel_imap_command (store, folder, ex,
					       "FETCH %d (%s)", first,
					       summary_specifier);
	} else {
		response = camel_imap_command (store, folder, ex,
					       "FETCH %d:%d (%s)", first,
					       last, summary_specifier);
	}
	g_free (summary_specifier);

	if (!response)
		return;

	headers = response->untagged;
	for (i = 0; i < headers->len; i++) {
		CamelMessageInfo *info;
		CamelImapMessageInfo *iinfo;
		char *uid, *flags, *header, *size;

		/* Grab the UID... */
		if (!(uid = strstr (headers->pdata[i], "UID "))) {
			d(fprintf (stderr, "Cannot get a uid for %d\n\n%s\n\n", i+1, (char *) headers->pdata[i]));
			break;
		}

		for (uid += 4; *uid && (*uid < '0' || *uid > '9'); uid++)
			;
		for (q = uid; *q && *q >= '0' && *q <= '9'; q++)
			;

		/* construct the header list */
		/* fast-forward to beginning of header info... */
		header = strchr (headers->pdata[i], '\n') + 1;
		h = NULL;
		do {
			char *line;
			int len;

			len = strcspn (header, "\n");
			while (header[len + 1] == ' ' ||
			       header[len + 1] == '\t')
				len += 1 + strcspn (header + len + 1, "\n");
			line = g_strndup (header, len);
			header_raw_append_parse (&h, line, -1);
			g_free (line);

			header += len;
		} while (*header++ == '\n' && *header != '\n');

		/* We can't just call camel_folder_summary_add_from_parser
		 * because it will assign the wrong UID, and thus get the
		 * uid hash table wrong and all that. FIXME some day.
		 */
		info = camel_folder_summary_info_new_from_header (
			imap_folder->summary, h);
		iinfo = (CamelImapMessageInfo *)info;
		header_raw_clear (&h);
		uid = g_strndup (uid, q - uid);
		camel_folder_change_info_add_uid (changes, uid);
		camel_message_info_set_uid (info, uid);

		/* now lets grab the FLAGS */
		if (!(flags = strstr (headers->pdata[i], "FLAGS "))) {
			d(fprintf (stderr, "We didn't seem to get any flags for %d...\n", i));
		} else {
			for (flags += 6; *flags && *flags != '('; flags++)
				;
			info->flags = imap_parse_flag_list (flags);
			iinfo->server_flags = info->flags;
		}

		/* And size */
		if (!(size = strstr (headers->pdata[i], "RFC822.SIZE "))) {
			d(fprintf (stderr, "We didn't seem to get any size for %d...\n", i));
		} else
			info->size = strtoul (size + 12, NULL, 10);

		camel_folder_summary_add (imap_folder->summary, info);
	}
	camel_imap_response_free (response);
}

static GPtrArray *
imap_get_summary (CamelFolder *folder)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);

	return imap_folder->summary->messages;
}

/* get a single message info, by uid */
static const CamelMessageInfo *
imap_get_message_info (CamelFolder *folder, const char *uid)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	
	return camel_folder_summary_uid (imap_folder->summary, uid);
}

static GPtrArray *
imap_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelImapResponse *response;
	GPtrArray *uids = NULL;
	char *result, *sexp, *p;
	
	d(fprintf (stderr, "camel sexp: '%s'\n", expression));
	sexp = imap_translate_sexp (expression);
	d(fprintf (stderr, "imap sexp: '%s'\n", sexp));
	
	uids = g_ptr_array_new ();
	
	if (!folder->has_search_capability) {
		g_free (sexp);
		return uids;
	}
	
	response = camel_imap_command (CAMEL_IMAP_STORE (folder->parent_store),
				       folder, NULL, "UID SEARCH %s", sexp);
	g_free (sexp);
	if (!response)
		return uids;

	result = camel_imap_response_extract (response, "SEARCH", NULL);
	if (!result)
		return uids;
	
	if ((p = strstr (result, "* SEARCH"))) {
		char *word;
		
		word = imap_next_word (p); /* word now points to SEARCH */
		
		for (word = imap_next_word (word); *word && *word != '*'; word = imap_next_word (word)) {
			gboolean word_is_numeric = TRUE;
			char *ep;
			
			/* find the end of this word and make sure it's a numeric uid */
			for (ep = word; *ep && *ep != ' ' && *ep != '\n'; ep++)
				if (*ep < '0' || *ep > '9')
					word_is_numeric = FALSE;
			
			if (word_is_numeric)
				g_ptr_array_add (uids, g_strndup (word, (gint)(ep - word)));
		}
	}
	
	g_free (result);
	
	return uids;
}

static guint32
imap_get_message_flags (CamelFolder *folder, const char *uid)
{
	const CamelMessageInfo *info;
	
	info = imap_get_message_info (folder, uid);
	g_return_val_if_fail (info != NULL, 0);
	
	return info->flags;
}

static void
imap_set_message_flags (CamelFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	CamelImapFolder *imap_folder = (CamelImapFolder *)folder;
	CamelMessageInfo *info;
	guint32 new;

	info = camel_folder_summary_uid (imap_folder->summary, uid);
	g_return_if_fail (info != NULL);

	new = (info->flags & ~flags) | (set & flags);
	if (new == info->flags)
		return;

	info->flags = new | CAMEL_MESSAGE_FOLDER_FLAGGED;
	camel_folder_summary_touch (imap_folder->summary);

	camel_object_trigger_event (CAMEL_OBJECT (folder), "message_changed",
				    (gpointer)uid);
}

static gboolean
imap_get_message_user_flag (CamelFolder *folder, const char *uid, const char *name)
{
	/* FIXME */
	return FALSE;
}

static void
imap_set_message_user_flag (CamelFolder *folder, const char *uid, const char *name, gboolean value)
{
	/* FIXME */
	camel_object_trigger_event (CAMEL_OBJECT (folder), "message_changed",
				    (gpointer)uid);
}

void
camel_imap_folder_changed (CamelFolder *folder, int exists,
			   GArray *expunged, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelFolderChangeInfo *changes;
	CamelMessageInfo *info;

	changes = camel_folder_change_info_new ();
	if (expunged) {
		int i, id;

		for (i = 0; i < expunged->len; i++) {
			id = g_array_index (expunged, int, i);
			info = camel_folder_summary_index (imap_folder->summary, id - 1);
			camel_folder_change_info_remove_uid (changes, camel_message_info_uid (info));
			camel_folder_summary_remove (imap_folder->summary, info);
		}
	}

	imap_update_summary (folder, imap_folder->exists + 1, exists,
			     changes, ex);
	imap_folder->exists = exists;

	if (camel_folder_change_info_changed (changes)) {
		camel_object_trigger_event (CAMEL_OBJECT (folder),
					    "folder_changed", changes);
	}
	camel_folder_change_info_free (changes);
}
