/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-session.h : Abstract class for an email session */

/*
 *
 * Author :
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
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


#ifndef CAMEL_SESSION_H
#define CAMEL_SESSION_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <camel/camel-object.h>
#include <camel/camel-provider.h>

#define CAMEL_SESSION_TYPE     (camel_session_get_type ())
#define CAMEL_SESSION(obj)     (GTK_CHECK_CAST((obj), CAMEL_SESSION_TYPE, CamelSession))
#define CAMEL_SESSION_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), CAMEL_SESSION_TYPE, CamelSessionClass))
#define CAMEL_IS_SESSION(o)    (GTK_CHECK_TYPE((o), CAMEL_SESSION_TYPE))


typedef enum {
	CAMEL_AUTHENTICATOR_ASK, CAMEL_AUTHENTICATOR_TELL
} CamelAuthCallbackMode;

typedef char *(*CamelAuthCallback) (CamelAuthCallbackMode mode,
				    char *data, gboolean secret,
				    CamelService *service, char *item,
				    CamelException *ex);

struct _CamelSession
{
	CamelObject parent_object;

	CamelAuthCallback authenticator;

	GHashTable *providers,
		*modules,
		*service_cache;
};

typedef struct {
	CamelObjectClass parent_class;

} CamelSessionClass;


/* public methods */

/* Standard Gtk function */
GtkType camel_session_get_type (void);


CamelSession *  camel_session_new                     (CamelAuthCallback
						       authenticator);

void            camel_session_register_provider       (CamelSession *session,
						       CamelProvider *provider);
GList *         camel_session_list_providers          (CamelSession *session,
						       gboolean load);

CamelService *  camel_session_get_service             (CamelSession *session,
						       const char *url_string,
						       CamelProviderType type,
						       CamelException *ex);
#define camel_session_get_store(session, url_string, ex) \
	((CamelStore *) camel_session_get_service (session, url_string, CAMEL_PROVIDER_STORE, ex))
#define camel_session_get_transport(session, url_string, ex) \
	((CamelTransport *) camel_session_get_service (session, url_string, CAMEL_PROVIDER_TRANSPORT, ex))


char *          camel_session_query_authenticator (CamelSession *session,
						   CamelAuthCallbackMode mode,
						   char *prompt,
						   gboolean secret,
						   CamelService *service,
						   char *item,
						   CamelException *ex);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_SESSION_H */
