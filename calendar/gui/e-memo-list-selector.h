/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-memo-list-selector.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* XXX This widget is nearly identical to ETaskListSelector.  If
 *     ECalendarSelector ever learns how to move selections from
 *     one source to another, perhaps these ESourceSelector sub-
 *     classes could someday be combined. */

#ifndef E_MEMO_LIST_SELECTOR_H
#define E_MEMO_LIST_SELECTOR_H

#include <e-util/e-util.h>
#include <shell/e-shell-view.h>

/* Standard GObject macros */
#define E_TYPE_MEMO_LIST_SELECTOR \
	(e_memo_list_selector_get_type ())
#define E_MEMO_LIST_SELECTOR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MEMO_LIST_SELECTOR, EMemoListSelector))
#define E_MEMO_LIST_SELECTOR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MEMO_LIST_SELECTOR, EMemoListSelectorClass))
#define E_IS_MEMO_LIST_SELECTOR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MEMO_LIST_SELECTOR))
#define E_IS_MEMO_LIST_SELECTOR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MEMO_LIST_SELECTOR))
#define E_MEMO_LIST_SELECTOR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MEMO_LIST_SELECTOR, EMemoListSelectorClass))

G_BEGIN_DECLS

typedef struct _EMemoListSelector EMemoListSelector;
typedef struct _EMemoListSelectorClass EMemoListSelectorClass;
typedef struct _EMemoListSelectorPrivate EMemoListSelectorPrivate;

struct _EMemoListSelector {
	EClientSelector parent;
	EMemoListSelectorPrivate *priv;
};

struct _EMemoListSelectorClass {
	EClientSelectorClass parent_class;
};

GType		e_memo_list_selector_get_type	(void);
GtkWidget *	e_memo_list_selector_new	(EClientCache *client_cache,
						 EShellView *shell_view);
EShellView *	e_memo_list_selector_get_shell_view
						(EMemoListSelector *selector);

G_END_DECLS

#endif /* E_MEMO_LIST_SELECTOR_H */