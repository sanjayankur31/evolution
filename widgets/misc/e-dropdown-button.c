/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-dropdown-menu.c
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors:
 *   Ettore Perazzoli <ettore@ximian.com>
 *   Damon Chaplin <damon@ximian.com> 
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <gtk/gtk.h>
#include <gal/util/e-util.h>

#include "e-dropdown-button.h"


struct _EDropdownButtonPrivate {
	GtkAccelGroup *accel_group;
	GtkWidget *menu;
};

#define PARENT_TYPE gtk_button_get_type ()
static GtkButtonClass *parent_class = NULL;


/* Callback to position the pop-up menu.  */

static void
menu_position_cb (GtkMenu *menu,
		  int *x,
		  int *y,
		  void *data)
{
	EDropdownButton *dropdown_button;
	EDropdownButtonPrivate *priv;
	GtkRequisition menu_requisition;
	int max_x, max_y;

	dropdown_button = E_DROPDOWN_BUTTON (data);
	priv = dropdown_button->priv;

	/* Calculate our preferred position.  */
	gdk_window_get_origin (GTK_WIDGET (dropdown_button)->window, x, y);
	*y += GTK_WIDGET (dropdown_button)->allocation.height;

	/* Now make sure we are on the screen.  */
	gtk_widget_size_request (GTK_WIDGET (priv->menu), &menu_requisition);
	max_x = MAX (0, gdk_screen_width () - menu_requisition.width);
	max_y = MAX (0, gdk_screen_height () - menu_requisition.height);

	*x = CLAMP (*x, 0, max_x);
	*y = CLAMP (*y, 0, max_y);
}


/* GtkObject methods.  */

static void
impl_destroy (GtkObject *object)
{
	EDropdownButton *dropdown_button;
	EDropdownButtonPrivate *priv;

	dropdown_button = E_DROPDOWN_BUTTON (object);
	priv = dropdown_button->priv;

	gtk_accel_group_unref (priv->accel_group);
	gtk_widget_destroy (priv->menu);

	g_free (priv);
}


/* GtkButton methods.  */

static void
impl_clicked (GtkButton *button)
{
	EDropdownButton *dropdown_button;
	EDropdownButtonPrivate *priv;

	dropdown_button = E_DROPDOWN_BUTTON (button);
	priv = dropdown_button->priv;

	if (GTK_BUTTON_CLASS (parent_class)->clicked != NULL)
		(* GTK_BUTTON_CLASS (parent_class)->clicked) (button);

	gtk_menu_popup (GTK_MENU (priv->menu), NULL, NULL,
			menu_position_cb, dropdown_button,
			1, GDK_CURRENT_TIME);
}


static void
class_init (EDropdownButtonClass *klass)
{
	GtkObjectClass *object_class;
	GtkButtonClass *button_class;

	object_class = GTK_OBJECT_CLASS (klass);
	button_class = GTK_BUTTON_CLASS (klass);

	object_class->destroy = impl_destroy;

	button_class->clicked = impl_clicked;

	parent_class = gtk_type_class (gtk_button_get_type ());
}


static void
init (EDropdownButton *dropdown_button)
{
	EDropdownButtonPrivate *priv;

	priv = g_new (EDropdownButtonPrivate, 1);
	priv->accel_group = gtk_accel_group_new ();
	priv->menu        = NULL;

	dropdown_button->priv = priv;
}


void
e_dropdown_button_construct (EDropdownButton *dropdown_button,
			     const char *label_text,
			     GtkMenu *menu,
			     GnomeUIInfo *ui_info,
			     void *data)
{
	EDropdownButtonPrivate *priv;
	GtkWidget *hbox;
	GtkWidget *arrow;
	GtkWidget *label;
	unsigned int accel_key;

	g_return_if_fail (dropdown_button != NULL);
	g_return_if_fail (E_IS_DROPDOWN_BUTTON (dropdown_button));
	g_return_if_fail (label_text != NULL);
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));
	g_return_if_fail (ui_info != NULL);

	priv = dropdown_button->priv;

	hbox = gtk_hbox_new (FALSE, 2);
	gtk_container_add (GTK_CONTAINER (dropdown_button), hbox);
	gtk_widget_show (hbox);

	label = gtk_label_new ("");
	accel_key = gtk_label_parse_uline (GTK_LABEL (label), label_text);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
	gtk_widget_show (label);
	gtk_widget_add_accelerator (GTK_WIDGET (dropdown_button), "clicked",
				    priv->accel_group, accel_key, GDK_MOD1_MASK, 0);

	arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_OUT);
	gtk_box_pack_start (GTK_BOX (hbox), arrow, FALSE, FALSE, 2);
	gtk_widget_show (arrow);

	priv->menu = GTK_WIDGET (menu);
	gnome_app_fill_menu_with_data (GTK_MENU_SHELL (priv->menu), ui_info,
				       priv->accel_group, TRUE, 0, data);
}

/**
 * e_dropdown_button_new:
 * @label_text: 
 * @menu: 
 * @ui_info: 
 * @data: 
 * 
 * Create a new dropdown button.
 * 
 * Return value: 
 **/
GtkWidget *
e_dropdown_button_new (const char *label_text,
		       GtkMenu *menu,
		       GnomeUIInfo *ui_info,
		       void *data)
{
	GtkWidget *widget;

	g_return_val_if_fail (label_text != NULL, NULL);
	g_return_val_if_fail (menu != NULL, NULL);
	g_return_val_if_fail (GTK_IS_MENU (menu), NULL);
	g_return_val_if_fail (ui_info != NULL, NULL);

	widget = gtk_type_new (e_dropdown_button_get_type ());

	e_dropdown_button_construct (E_DROPDOWN_BUTTON (widget), label_text, menu, ui_info, data);

	return widget;
}


E_MAKE_TYPE (e_dropdown_button, "EDropdownButton", EDropdownButton, class_init, init, PARENT_TYPE)
