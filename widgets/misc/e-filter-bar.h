/*
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
 *		Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef E_FILTER_BAR_H
#define E_FILTER_BAR_H

#include <gtk/gtk.h>
#include <camel/camel-vee-folder.h>
#include <camel/camel-operation.h>

#include "e-search-bar.h"

#include "filter/rule-context.h"
#include "filter/filter-rule.h"

/* EFilterBar - A filter rule driven search bar.
 *
 * The following arguments are available:
 *
 * name		 type		read/write	description
 * ---------------------------------------------------------------------------------
 * query         string         R               String representing query.
 * state         string         RW              XML string representing the state.
 */

/* Standard GObject macros */
#define E_TYPE_FILTER_BAR \
	(e_filter_bar_get_type ())
#define E_FILTER_BAR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_FILTER_BAR, EFilterBar))
#define E_FILTER_BAR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_FILTER_BAR, EFilterBarClass))
#define E_IS_FILTER_BAR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_FILTER_BAR))
#define E_IS_FILTER_BAR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((obj), E_TYPE_FILTER_BAR))
#define E_FILTER_BAR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_FILTER_BAR, EFilterBarClass))

G_BEGIN_DECLS

typedef struct _EFilterBar EFilterBar;
typedef struct _EFilterBarClass EFilterBarClass;

typedef void (*EFilterBarConfigRule)(EFilterBar *, FilterRule *rule, int id, const char *query, void *data);

struct _EFilterBar {
	ESearchBar parent;
	int menu_base, option_base;
	GPtrArray *menu_rules, *option_rules;

	ESearchBarItem *default_items;

	GtkWidget *save_dialog;    /* current save dialogue (so we dont pop up multiple ones) */

	FilterRule *current_query; /* as it says */
	int setquery;		   /* true when we're setting a query directly to advanced, so dont popup the dialog */

	RuleContext *context;
	char *systemrules;
	char *userrules;

	EFilterBarConfigRule config;
	void *config_data;

	CamelVeeFolder *all_account_search_vf;
	CamelVeeFolder *account_search_vf;
	CamelOperation *account_search_cancel;
};

struct _EFilterBarClass {
	ESearchBarClass parent_class;
};

/* "preset" items */
enum {
	/* preset menu options */
	E_FILTERBAR_SAVE_ID = -3,
	E_FILTERBAR_EDIT_ID = -4,

	/* preset option options */
	E_FILTERBAR_ADVANCED_ID = -5,
	E_FILTERBAR_CURRENT_FOLDER_ID = -7,
	E_FILTERBAR_CURRENT_ACCOUNT_ID = -8,
	E_FILTERBAR_ALL_ACCOUNTS_ID = -9
};

#define E_FILTERBAR_SAVE      { N_("_Save Search..."), E_FILTERBAR_SAVE_ID, 0 }
#define E_FILTERBAR_EDIT      { N_("_Edit Saved Searches..."), E_FILTERBAR_EDIT_ID, 0 }
#define E_FILTERBAR_ADVANCED  { N_("_Advanced Search..."), E_FILTERBAR_ADVANCED_ID, 0 }
#define E_FILTERBAR_ALL_ACCOUNTS { N_("All Accounts"), E_FILTERBAR_ALL_ACCOUNTS_ID, ESB_ITEMTYPE_RADIO }
#define E_FILTERBAR_CURRENT_ACCOUNT { N_("Current Account"), E_FILTERBAR_CURRENT_ACCOUNT_ID, ESB_ITEMTYPE_RADIO }
#define E_FILTERBAR_CURRENT_FOLDER { N_("Current Folder"), E_FILTERBAR_CURRENT_FOLDER_ID, ESB_ITEMTYPE_RADIO }
#define E_FILTERBAR_SEPARATOR { NULL, 0, 0 }

#ifdef JUST_FOR_TRANSLATORS
const char * strings[] = {
	N_("_Save Search..."),
	N_("_Edit Saved Searches..."),
	N_("_Advanced Search...")
};
#endif


GType		e_filter_bar_get_type		(void);
EFilterBar *	e_filter_bar_new		(RuleContext *context,
						 const gchar *systemrules,
						 const gchar *userrules,
						 EFilterBarConfigRule config,
						 gpointer data);

G_END_DECLS

#endif /* E_FILTER_BAR_H */
