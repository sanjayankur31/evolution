/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <config.h>
#include "e-contact-list-model.h"

#define PARENT_TYPE e_table_model_get_type()
ETableModelClass *parent_class;

#define COLS 1

/* This function returns the number of columns in our ETableModel. */
static int
contact_list_col_count (ETableModel *etc)
{
	return COLS;
}

/* This function returns the number of rows in our ETableModel. */
static int
contact_list_row_count (ETableModel *etc)
{
	EContactListModel *model = E_CONTACT_LIST_MODEL (etc);
	return model->simple_count + model->email_count;
}

/* This function returns the value at a particular point in our ETableModel. */
static void *
contact_list_value_at (ETableModel *etc, int col, int row)
{
	EContactListModel *model = E_CONTACT_LIST_MODEL (etc);

	if (row < model->simple_count)
		return (char*)e_card_simple_get_const (model->simples[row], E_CARD_SIMPLE_FIELD_EMAIL);
	else
		return model->emails [row - model->simple_count];
}

/* This function sets the value at a particular point in our ETableModel. */
static void
contact_list_set_value_at (ETableModel *etc, int col, int row, const void *val)
{
	/* nothing */
}

/* This function returns whether a particular cell is editable. */
static gboolean
contact_list_is_cell_editable (ETableModel *etc, int col, int row)
{
	return FALSE;
}

/* This function duplicates the value passed to it. */
static void *
contact_list_duplicate_value (ETableModel *etc, int col, const void *value)
{
	return g_strdup(value);
}

/* This function frees the value passed to it. */
static void
contact_list_free_value (ETableModel *etc, int col, void *value)
{
	g_free(value);
}

static void *
contact_list_initialize_value (ETableModel *etc, int col)
{
	return g_strdup("");
}

static gboolean
contact_list_value_is_empty (ETableModel *etc, int col, const void *value)
{
	return !(value && *(char *)value);
}

static char *
contact_list_value_to_string (ETableModel *etc, int col, const void *value)
{
	return g_strdup(value);
}

static void
contact_list_model_destroy (GtkObject *o)
{
}

static void
e_contact_list_model_class_init (GtkObjectClass *object_class)
{
	ETableModelClass *model_class = (ETableModelClass *) object_class;

	parent_class = gtk_type_class (PARENT_TYPE);

	object_class->destroy = contact_list_model_destroy;

	model_class->column_count = contact_list_col_count;
	model_class->row_count = contact_list_row_count;
	model_class->value_at = contact_list_value_at;
	model_class->set_value_at = contact_list_set_value_at;
	model_class->is_cell_editable = contact_list_is_cell_editable;
	model_class->duplicate_value = contact_list_duplicate_value;
	model_class->free_value = contact_list_free_value;
	model_class->initialize_value = contact_list_initialize_value;
	model_class->value_is_empty = contact_list_value_is_empty;
	model_class->value_to_string = contact_list_value_to_string;
}

static void
e_contact_list_model_init (GtkObject *object)
{
	EContactListModel *model = E_CONTACT_LIST_MODEL(object);

	model->simples = NULL;
	model->simple_count = 0;
	model->emails = NULL;
	model->email_count = 0;
}

GtkType
e_contact_list_model_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"EContactListModel",
			sizeof (EContactListModel),
			sizeof (EContactListModelClass),
			(GtkClassInitFunc) e_contact_list_model_class_init,
			(GtkObjectInitFunc) e_contact_list_model_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (PARENT_TYPE, &info);
	}

	return type;
}

void
e_contact_list_model_construct (EContactListModel *model)
{
}

ETableModel *
e_contact_list_model_new ()
{
	EContactListModel *model;

	model = gtk_type_new (e_contact_list_model_get_type ());

	e_contact_list_model_construct (model);

	return E_TABLE_MODEL(model);
}

void
e_contact_list_model_add_email (EContactListModel *model,
				const char *email)
{
	model->email_count ++;
	model->emails = g_renew (char*, model->emails, model->email_count);
	model->emails[model->email_count - 1] = g_strdup (email);
	e_table_model_changed (E_TABLE_MODEL (model));
}

void
e_contact_list_model_add_card (EContactListModel *model,
			       ECardSimple *simple)
{
}

void
e_contact_list_model_remove_row (EContactListModel *model, int row)
{
	if (row < model->simple_count) {
		memcpy (model->simples + row, model->simples + row + 1, model->simple_count - row);
		model->simple_count --;
	}
	else {
		int email_row = row - model->simple_count;
		memcpy (model->emails + email_row, model->emails + email_row + 1, model->email_count - email_row);
		model->email_count --;
	}
	e_table_model_row_deleted (E_TABLE_MODEL (model), row);
}
