/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2004 Jeffrey Stedfast
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */


#ifndef __CAMEL_IMAP_STREAM_H__
#define __CAMEL_IMAP_STREAM_H__

#include <camel/camel-stream.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define CAMEL_TYPE_IMAP_STREAM     (camel_imap_stream_get_type ())
#define CAMEL_IMAP_STREAM(obj)     (CAMEL_CHECK_CAST ((obj), CAMEL_TYPE_IMAP_STREAM, CamelIMAPStream))
#define CAMEL_IMAP_STREAM_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_TYPE_IMAP_STREAM, CamelIMAPStreamClass))
#define CAMEL_IS_IMAP_STREAM(o)    (CAMEL_CHECK_TYPE((o), CAMEL_TYPE_IMAP_STREAM))

typedef struct _CamelIMAPStream CamelIMAPStream;
typedef struct _CamelIMAPStreamClass CamelIMAPStreamClass;

#define IMAP_READ_PRELEN   128
#define IMAP_READ_BUFLEN   4096

enum {
	CAMEL_IMAP_TOKEN_NO_DATA       = -8,
	CAMEL_IMAP_TOKEN_ERROR         = -7,
	CAMEL_IMAP_TOKEN_NIL           = -6,
	CAMEL_IMAP_TOKEN_ATOM          = -5,
	CAMEL_IMAP_TOKEN_FLAG          = -4,
	CAMEL_IMAP_TOKEN_NUMBER        = -3,
	CAMEL_IMAP_TOKEN_QSTRING       = -2,
	CAMEL_IMAP_TOKEN_LITERAL       = -1,
	/* CAMEL_IMAP_TOKEN_CHAR would just be the char we got */
	CAMEL_IMAP_TOKEN_EOLN          = '\n',
	CAMEL_IMAP_TOKEN_LPAREN        = '(',
	CAMEL_IMAP_TOKEN_RPAREN        = ')',
	CAMEL_IMAP_TOKEN_ASTERISK      = '*',
	CAMEL_IMAP_TOKEN_PLUS          = '+',
	CAMEL_IMAP_TOKEN_LBRACKET      = '[',
	CAMEL_IMAP_TOKEN_RBRACKET      = ']',
};

typedef struct _camel_imap_token_t {
	int token;
	union {
		char *atom;
		char *flag;
		char *qstring;
		size_t literal;
		guint32 number;
	} v;
} camel_imap_token_t;

enum {
	CAMEL_IMAP_STREAM_MODE_TOKEN   = 0,
	CAMEL_IMAP_STREAM_MODE_LITERAL = 1,
};

struct _CamelIMAPStream {
	CamelStream parent_object;
	
	CamelStream *stream;
	
	guint disconnected:1;  /* disconnected state */
	guint mode:1;          /* TOKEN vs LITERAL */
	guint eol:1;           /* end-of-literal */
	
	size_t literal;
	
	/* i/o buffers */
	unsigned char realbuf[IMAP_READ_PRELEN + IMAP_READ_BUFLEN + 1];
	unsigned char *inbuf;
	unsigned char *inptr;
	unsigned char *inend;
	
	/* token buffers */
	unsigned char *tokenbuf;
	unsigned char *tokenptr;
	unsigned int tokenleft;
	
	camel_imap_token_t *unget;
};

struct _CamelIMAPStreamClass {
	CamelStreamClass parent_class;
	
	/* Virtual methods */
};


/* Standard Camel function */
CamelType camel_imap_stream_get_type (void);

CamelStream *camel_imap_stream_new (CamelStream *stream);

int camel_imap_stream_next_token (CamelIMAPStream *stream, camel_imap_token_t *token);
int camel_imap_stream_unget_token (CamelIMAPStream *stream, camel_imap_token_t *token);

int camel_imap_stream_line (CamelIMAPStream *stream, unsigned char **line, size_t *len);
int camel_imap_stream_literal (CamelIMAPStream *stream, unsigned char **literal, size_t *len);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __CAMEL_IMAP_STREAM_H__ */
