/* -*- mode: C; mode: fold -*- */
/* MIME handling routines.
 *
 * Author: Michael Elkins <elkins@aero.org>
 * Modified by John E. Davis <davis@space.mit.edu>
 * Modified by Thomas Schultz <tststs@gmx.de>
 * 
 * Change Log:
 * Aug 20, 1997 patch from "Byrial Jensen" <byrial@post3.tele.dk>
 *   added.  Apparantly RFC2047 requires the whitespace separating
 *   multiple encoded words in headers to be ignored.
 *   Status: unchecked
 */

#include "config.h"
#ifndef SLRNPULL_CODE
#include "slrnfeat.h"
#endif

#include <stdio.h>
#include <string.h>


#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <ctype.h>

#if defined(__os2__) || defined(__NT__)
# include <process.h>
#endif

#include <slang.h>
#include "jdmacros.h"

#include "slrn.h"
#include "misc.h"
#include "slrn.h"
#include "group.h"
#include "art.h"
#include "util.h"
#include "strutil.h"
#include "server.h"
#include "snprintf.h"
#include "mime.h"
#include "charset.h"
#include "common.h"

int Slrn_Use_Meta_Mail = 1;
int Slrn_Fold_Headers = 1;
char *Slrn_MetaMail_Cmd;

#ifndef SLRNPULL_CODE
#define CONTENT_TYPE_TEXT		0x01
#define CONTENT_TYPE_MESSAGE		0x02
#define CONTENT_TYPE_MULTIPART		0x03
#define CONTENT_TYPE_UNSUPPORTED	0x10

#define CONTENT_SUBTYPE_PLAIN		0x01
#define CONTENT_SUBTYPE_UNKNOWN		0x02
#define CONTENT_SUBTYPE_UNSUPPORTED	0x10


static Slrn_Article_Line_Type *find_header_line (Slrn_Article_Type *a, char *header)/*{{{*/
{
   Slrn_Article_Line_Type *line;
   unsigned char ch = (unsigned char) UPPER_CASE(*header);
   unsigned int len = strlen (header);

   if (a == NULL)
     return NULL;
   line = a->lines;

   while ((line != NULL) && (line->flags & HEADER_LINE))
     {
	unsigned char ch1 = (unsigned char) *line->buf;
	if ((ch == UPPER_CASE(ch1))
	    && (0 == slrn_case_strncmp (header,
					line->buf,
					len)))
	  return line;
	line = line->next;
     }
   return NULL;
}
/*}}}*/
#endif /* NOT SLRNPULL_CODE */

#ifndef SLRNPULL_CODE
/*
 * Returns 0 for supported Content-Types:
 *   text/plain
 *   message/
 *   multipart/
 * Otherwise it returns -1.
 */
static int parse_content_type_line (Slrn_Article_Type *a)/*{{{*/
{
   char *b;
   Slrn_Article_Line_Type *line;
   
   if (a == NULL)
     return -1;
   line = a->lines;
   
   if (NULL == (line = find_header_line (a, "Content-Type:")))
     return 0;
   
   b = slrn_skip_whitespace (line->buf + 13);
   
   if (0 == slrn_case_strncmp (b,
			        "text/",
			       5))
     {
	a->mime.content_type = CONTENT_TYPE_TEXT;
	b += 5;
	if (0 != slrn_case_strncmp (b,
				     "plain",
				    5))
	  {
	     a->mime.content_subtype = CONTENT_SUBTYPE_UNSUPPORTED;
	     return -1;
	  }
	else
	  {
	     a->mime.content_subtype = CONTENT_SUBTYPE_PLAIN;
	  }
	b += 5;
     }
   else if (0 == slrn_case_strncmp (b,
				     "message/",
				    5))
     {
	a->mime.content_type = CONTENT_TYPE_MESSAGE;
	a->mime.content_subtype = CONTENT_SUBTYPE_UNKNOWN;
	b += 8;
     }
   else if (0 == slrn_case_strncmp (b,
				     "multipart/",
				    5))
     {
	a->mime.content_type = CONTENT_TYPE_MULTIPART;
	a->mime.content_subtype = CONTENT_SUBTYPE_UNKNOWN;
	b += 10;
     }
   else
     {
	a->mime.content_type = CONTENT_TYPE_UNSUPPORTED;
	return -1;
     }
   
   do
     {
	while (NULL != (b = slrn_strbyte (b, ';')))
	  {
	     char *charset;
	     unsigned int len;
	     
	     b = slrn_skip_whitespace (b + 1);
	     
	     if (0 != slrn_case_strncmp (b,
					 "charset",
					 7))
	       continue;
	     
	     b = slrn_skip_whitespace (b + 7);
	     while (*b == 0)
	       {
		  line = line->next;
		  if ((line == NULL)
		      || ((line->flags & HEADER_LINE) == 0)
		      || ((*(b = line->buf) != ' ') && (*b == '\t')))
		    return -1;
		  b = slrn_skip_whitespace (b);
	       }
	     
	     if (*b != '=') continue;
	     b++;
	     if (*b == '"') b++;
	     charset = b;
	     while (*b && (*b != ';')
		    && (*b != ' ') && (*b != '\t') && (*b != '\n')
		    && (*b != '"'))
	       b++;
	     len = b - charset;
	     
	     a->mime.charset = slrn_safe_strnmalloc (charset, len);
	     return 0;
	  }
	line = line->next;
     }
   while ((line != NULL)
	  && (line->flags & HEADER_LINE)
	  && ((*(b = line->buf) == ' ') || (*b == '\t')));
   
   return 0;
}

/*}}}*/
#define ENCODED_RAW			0
#define ENCODED_7BIT			1
#define ENCODED_8BIT			2
#define ENCODED_QUOTED			3
#define ENCODED_BASE64			4
#define ENCODED_BINARY			5
#define ENCODED_UNSUPPORTED		6
static int parse_content_transfer_encoding_line (Slrn_Article_Type *a)/*{{{*/
{
   Slrn_Article_Line_Type *line;
   char *buf;
   
   if (a == NULL)
     return -1;

   line = find_header_line (a, "Content-Transfer-Encoding:");
   if (line == NULL) return ENCODED_RAW;

   buf = slrn_skip_whitespace (line->buf + 26);
   if (*buf == '"') buf++;
   
   if (0 == slrn_case_strncmp (buf,  "7bit", 4))
	return ENCODED_7BIT;
   else if (0 == slrn_case_strncmp (buf,  "8bit", 4))
	return ENCODED_8BIT;
   else if (0 == slrn_case_strncmp (buf,  "base64", 6))
	return ENCODED_BASE64;
   else if (0 == slrn_case_strncmp (buf,  "quoted-printable", 16))
	return ENCODED_QUOTED;
   else if (0 == slrn_case_strncmp (buf,  "binary", 6))
	return ENCODED_BINARY;
   return ENCODED_UNSUPPORTED;
}

/*}}}*/
#endif /* NOT SLRNPULL_CODE*/

static int Index_Hex[128] =/*{{{*/
{
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
     -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
     -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
     0, 1, 2, 3,  4, 5, 6, 7,  8, 9,-1,-1, -1,-1,-1,-1,
     -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
     -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
     -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
     -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
};

/*}}}*/
#define HEX(c) (Index_Hex[(unsigned char)(c) & 0x7F])

static char *decode_quoted_printable (char *dest,/*{{{*/
				      char *src, char *srcmax,
				      int treat_underscore_as_space,
				      int keep_nl)
{
   char *allowed_in_qp = "0123456789ABCDEFabcdef";
   unsigned char ch;
/*
#ifndef SLRNPULL_CODE
   if (strip_8bit && (NULL == Char_Set))
     mask = 0x80;
#else
   (void) strip_8bit;
#endif*/
   while (src < srcmax)
     {
	ch = (unsigned char) *src++;
	if ((ch == '=') && (src + 1 < srcmax)
	    && (NULL != slrn_strbyte (allowed_in_qp, src[0]))
	    && (NULL != slrn_strbyte (allowed_in_qp, src[1])))
	  {
	     ch = (16 * HEX(src[0])) + HEX(src[1]);
	     if ((ch == '\n') && (keep_nl == 0))
	       ch = '?';
	     *dest++ = (char) ch;
	     src += 2;
	  }
	else if ((ch == '_') && treat_underscore_as_space)
	  {
	     *dest++ = ' ';
	  }
	else if ((ch == '\n') && (keep_nl == 0))
	  *dest++ = '?';
	else 
	  *dest++ = (char) ch;
     }
   return dest;
}

/*}}}*/

static int Index_64[128] =/*{{{*/
{
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
     -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
     -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
     52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-1,-1,-1,
     -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
     15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
     -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
     41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
};
/*}}}*/
#define BASE64(c) (Index_64[(unsigned char)(c) & 0x7F])

static char *decode_base64 (char *dest, char *src, char *srcmax, int keep_nl) /*{{{*/
{
   while (src + 3 < srcmax)
     {
	char ch = (BASE64(src[0]) << 2) | (BASE64(src[1]) >> 4);
	if ((ch == '\n') && (keep_nl == 0)) ch = '?';
	*dest++ = ch;
	
	if (src[2] == '=') break;
	ch = ((BASE64(src[1]) & 0xf) << 4) | (BASE64(src[2]) >> 2);
	if ((ch == '\n') && (keep_nl == 0)) ch = '?';
	*dest++ = ch;

	if (src[3] == '=') break;
	ch = ((BASE64(src[2]) & 0x3) << 6) | BASE64(src[3]);
	if ((ch == '\n') && (keep_nl == 0)) ch = '?';
	*dest++ = ch;

	src += 4;
     }
   return dest;
}

/*}}}*/

/* Warning: It must be ok to free *s_ptr and replace it with the converted
 * string */
int slrn_rfc1522_decode_string (char **s_ptr)/*{{{*/
{
   char *s1, *s2, ch, *s;
   char *charset, method, *txt;
   char *after_last_encoded_word;
   char *after_whitespace;
   unsigned int count;
   unsigned int len;
   int keep_nl = 0;

   count = 0;
   after_whitespace = NULL;
   after_last_encoded_word = NULL;
   charset = NULL;
   s= *s_ptr;

   if (slrn_string_nonascii(s))
	return -1;
   
   while (1)
     {
	char *decoded_start, *decoded_end;

	while ((NULL != (s = slrn_strbyte (s, '=')))
	       && (s[1] != '?')) s++;
	if (s == NULL) break;
	
	s1 = s;

	charset = s = s1 + 2;
	while (((ch = *s) != 0)
	       && (ch != '?') && (ch != ' ') && (ch != '\t') && (ch != '\n'))
	  s++;
	
	if (ch != '?')
	  {
	     s = s1 + 2;
	     charset = NULL;
	     continue;
	  }
	
	charset = s1 + 2;
	len = s - charset;
	charset=slrn_strnmalloc (charset, len, 1);
	
	s++;			       /* skip ? */
	method = *s++;		       /* skip B,Q */
	/* works in utf8 mode and else */
	if (method == 'b') method = 'B';
	if (method == 'q') method = 'Q';

	if ((charset == NULL) || ((method != 'B') && (method != 'Q'))
	    || (*s != '?'))
	  {
	     s = s1 + 2;
	     slrn_free(charset);
	     charset = NULL;
	     continue;
	  }
	/* Now look for the final ?= after encoded test */
	s++;			       /* skip ? */
	txt = s;
       
	while ((ch = *s) != 0)
	  {
	     /* Appararantly some programs do not convert ' ' to '_' in 
	      * quoted printable.  Sigh.
	      */
	     if (((ch == ' ') && (method != 'Q'))
		 || (ch == '\t') || (ch == '\n'))
	       break;
	     if ((ch == '?') && (s[1] == '='))
	       break;
	     
	     s++;
	  }
	
	if ((ch != '?') || (s[1] != '='))
	  {
	     s = s1 + 2;
	     slrn_free(charset);
	     charset = NULL;
	     continue;
	  }

	if (s1 == after_whitespace)
	  s1 = after_last_encoded_word;

        /* Note: these functions return a pointer to the END of the decoded
	 * text.
	 */
	s2 = s1;

	decoded_start = s1;

	if (method == 'B')
	  s1 = decode_base64 (s1, txt, s, keep_nl);
	else s1 = decode_quoted_printable (s1, txt, s, 1, keep_nl);

	decoded_end = s1;

	/* Now move everything over */
	s2 = s + 2;		       /* skip final ?= */
	s = s1;			       /* start from here next loop */
	while ((ch = *s2++) != 0) *s1++ = ch;
	*s1 = 0;
	
	count++;

	if (slrn_case_strncmp(Slrn_Display_Charset,
			   charset,
			   (strlen(Slrn_Display_Charset) <= len) ? strlen(Slrn_Display_Charset) : len) != 0)
	  {
	     /* We really need the position _after_ the decoded word, so we
	      * split the remainder of the string for charset conversion and
	      * put it back together afterward. */
	     char ch1;
	     unsigned int offset = decoded_start - *s_ptr;
	     unsigned int substr_len = decoded_end - decoded_start;

	     ch1 = *decoded_end;
	     *decoded_end = 0;
	     s2 = slrn_convert_substring(*s_ptr, offset, substr_len, Slrn_Display_Charset, charset, 0);
	     *decoded_end = ch1;
	     
	     if (s2 != NULL)
	       {
		  s = slrn_strdup_strcat (s2, decoded_end, NULL);
		  slrn_free(*s_ptr);
		  *s_ptr = s;
		  s += strlen(s2);
		  slrn_free(s2);
	       }
           }
	
	slrn_free(charset);
	charset=NULL;

	after_last_encoded_word = s;
	s = slrn_skip_whitespace (s);
	after_whitespace = s;
     }
   if (charset!=NULL)
	slrn_free(charset);
   return count;
}

/*}}}*/

#ifndef SLRNPULL_CODE /* rest of the file in this ifdef */

static void rfc1522_decode_headers (Slrn_Article_Type *a)/*{{{*/
{
   Slrn_Article_Line_Type *line;
   
   if (a == NULL)
     return;
   
   line = a->lines;

   while ((line != NULL) && (line->flags & HEADER_LINE))
     {
	if (slrn_case_strncmp (line->buf,
			       "Newsgroups:", 11) &&
	    slrn_case_strncmp (line->buf,
			       "Followup-To:", 12) &&
	    (slrn_rfc1522_decode_string (&line->buf) > 0))
	  {
	     a->is_modified = 1;
	     a->mime.was_modified = 1;
	  }
	line = line->next;
     }
}

/*}}}*/

static void decode_mime_base64 (Slrn_Article_Type *a)/*{{{*/
{
   Slrn_Article_Line_Type *l;
   Slrn_Article_Line_Type *body_start, *next;
   char *buf_src, *buf_dest, *buf_pos;
   char *base;
   int keep_nl = 1;
   int len;

   if (a == NULL) return;
   
   l = a->lines;
   
   /* skip header and separator */
   while ((l != NULL) && ((l->flags & HEADER_LINE) || l->buf[0] == '\0'))
     l = l->next;
   
   if (l == NULL) return;
   
   body_start = l;
   
   /* let's calculate how much space we need... */
   len = 0;
   while ( l )
     {
	len += strlen(l->buf);
	l = l->next;
     }
   
   /* get some memory */
   buf_src = slrn_safe_malloc (len + 1);
   buf_dest = slrn_safe_malloc (len + 1);
   
   /* collect all base64 encoded lines into buf_src */
   l = body_start;
   buf_pos = buf_src;
   while ( l )
     {
	strcat (buf_pos, l->buf); /* safe */
	buf_pos += strlen(l->buf);
	l = l->next;
     }
   
   /* put decoded article into buf_dest */
   buf_pos = decode_base64(buf_dest, buf_src, buf_src+len, keep_nl);
   *buf_pos = '\0';
   
   if (a->mime.charset == NULL)
     {
	buf_pos = buf_dest;
	while (*buf_pos)
	  {
	     if (*buf_pos & 0x80) *buf_pos = '?';
	     buf_pos++;
	  }
     }
   
   l = body_start;
   body_start = body_start->prev;
   
   /* free old body */
   while ( l )
     {
	slrn_free(l->buf);
	next = l->next;
	slrn_free((char *)l);
	l = next;
     }
   body_start->next = NULL;
   
   l = body_start;
   
   base = buf_dest;
   buf_pos = buf_dest;
   
   /* put decoded article back into article structure */
   while ( (buf_pos = slrn_strbyte(buf_dest, '\n')) != NULL )
     {
	len = buf_pos - buf_dest;
	
	l->next = (Slrn_Article_Line_Type *)
	  slrn_malloc(sizeof(Slrn_Article_Line_Type), 1, 1);
	
	l->next->prev = l;
	l->next->next = NULL;
	l = l->next;
	l->buf = slrn_malloc(sizeof(char) * len + 1, 0, 1);
	
	strncpy(l->buf, buf_dest, len);
	/* terminate string and strip '\r' if necessary */
	if ( l->buf[len-1] == '\r' )
	  l->buf[len-1] = '\0';
	else
	  l->buf[len] = '\0';
	
	buf_dest = buf_pos + 1;
     }
   
   slrn_free(buf_src);
   slrn_free(base);
   
   a->is_modified = 1;
   a->mime.was_modified = 1;
}

/*}}}*/

/* This function checks if the last character on curr_line is an = and 
 * if it is, then it merges curr_line and curr_line->next. See RFC1341,
 * section 5.1 (Quoted-Printable Content-Transfer-Encoding) rule #5.
 * [csp@ohm.york.ac.uk]
 */
static int merge_if_soft_linebreak (Slrn_Article_Line_Type *curr_line)/*{{{*/
{
   Slrn_Article_Line_Type *next_line;
   char *b;

   while ((next_line = curr_line->next) != NULL)
     {
	unsigned int len;

	b = curr_line->buf;
	len = (unsigned int) (slrn_bskip_whitespace (b) - b);
	if (len == 0) return 0;
   
	len--;
	if (b[len] != '=') return 0;
	
	/* Remove the excess = character... */
	b[len] = '\0';
	
	if (NULL == (b = (char *) SLrealloc (b, 1 + len + strlen (next_line->buf))))
	  return -1;

	curr_line->buf = b;
	
	strcpy (b + len, next_line->buf); /* safe */
	
	/* Unlink next_line from the linked list of lines in the article... */
	curr_line->next = next_line->next;
	if (next_line->next != NULL)
	  next_line->next->prev = curr_line;
	
	SLFREE (next_line->buf);
	SLFREE (next_line);
     }

   /* In case the last line ends with a soft linebreak: */
   b = slrn_bskip_whitespace (curr_line->buf);
   if (b != curr_line->buf)
     {
	b--;
	if (*b == '=')
	  *b = 0;
     }
   
   return 0;
}

/*}}}*/

static int split_qp_lines (Slrn_Article_Type *a)
{
   Slrn_Article_Line_Type *line;
   
   line = a->lines;

   /* skip header lines */
   while ((line != NULL) 
	  && (line->flags & HEADER_LINE))
     line=line->next;

   while (line != NULL)
     {
	Slrn_Article_Line_Type *new_line, *next;
	char *p = line->buf;
	char *buf0, *buf1;
	char ch;

	next = line->next;

	while ((0 != (ch = *p)) && (ch != '\n'))
	  p++;
	
	if (ch == 0)
	  {
	     line = next;
	     continue;
	  }

	new_line = (Slrn_Article_Line_Type *) slrn_malloc (sizeof(Slrn_Article_Line_Type), 1, 1);
	if (new_line == NULL)
	  return -1;

	if (NULL == (buf1 = slrn_strmalloc (p+1, 1)))
	  {
	     slrn_free ((char *) new_line);
	     return -1;
	  }
	
	if (NULL == (buf0 = slrn_realloc (line->buf, p-line->buf, 1)))
	  {
	     slrn_free ((char *) new_line);
	     slrn_free (buf1);
	     return -1;
	  }
	line->buf = buf0;

	new_line->buf = buf1;
	new_line->flags = line->flags;
	new_line->next = next;
	new_line->prev = line;
	if (next != NULL)
	  next->prev = new_line;
	line->next = new_line;

	line = next;
     }

   return 0;
}

static int decode_mime_quoted_printable (Slrn_Article_Type *a)/*{{{*/
{
   Slrn_Article_Line_Type *line;
   int keep_nl = 1;

   if (a == NULL)
     return -1;
   
   line = a->lines;

   /* skip to body */
   while ((line != NULL) && (line->flags & HEADER_LINE))
     line = line->next;

   if (line == NULL) 
     return 0;
   
   while (line != NULL)
     {
	char *b;
	unsigned int len;
	
	b = line->buf;
	len = (unsigned int) (slrn_bskip_whitespace (b) - b);
	if (len && (b[len - 1] == '=') 
	    && (line->next != NULL))
	  {
	     (void) merge_if_soft_linebreak (line);
	     b = line->buf;
	     len = strlen (b);
	  }

	b = decode_quoted_printable (b, b, b + len, 0, keep_nl);
	if (b < line->buf + len)
	  {
	     *b = 0;
	     a->is_modified = 1;
	     a->mime.was_modified = 1;
	  }
	
	line = line->next;
     }
   
   return split_qp_lines (a);
}

/*}}}*/

void slrn_mime_init (Slrn_Mime_Type *m)/*{{{*/
{
   m->was_modified = 0;
   m->was_parsed = 0;
   m->needs_metamail = 0;
   m->charset = NULL;
   m->content_type = 0;
   m->content_subtype = 0;
}

/*}}}*/

void slrn_mime_free (Slrn_Mime_Type *m)/*{{{*/
{
  if (m->charset != NULL)
    {
       slrn_free(m->charset);
    }
}

/*}}}*/

Slrn_Mime_Error_Obj *slrn_add_mime_error(Slrn_Mime_Error_Obj *list, /*{{{*/
					 char *msg, char *line, int lineno, int critical)
{
   Slrn_Mime_Error_Obj *err, *last;
   
   err = (Slrn_Mime_Error_Obj *)slrn_safe_malloc (sizeof (Slrn_Mime_Error_Obj));
   
   if (msg != NULL)
     err->msg = slrn_safe_strmalloc (msg);
   else
     err->msg = NULL;

   if (line != NULL)
     err->err_str = slrn_safe_strmalloc(line);
   else
     err->err_str = NULL;

   err->lineno=lineno;
   err->critical=critical;
   err->next = NULL;
   err->prev = NULL;
   
   if (list == NULL)
     return err;

   last = list;

   while (last->next != NULL)
     last = last->next;
   
   last->next = err;
   err->prev = last;

   return list;
}

/*}}}*/

Slrn_Mime_Error_Obj *slrn_mime_error (char *msg, char *line, int lineno, int critical)
{
   return slrn_add_mime_error (NULL, msg, line, lineno, critical);
}


Slrn_Mime_Error_Obj *slrn_mime_concat_errors (Slrn_Mime_Error_Obj *a, Slrn_Mime_Error_Obj *b)
{
   if (a == NULL)
     return b;
   
   if (b != NULL)
     {
	Slrn_Mime_Error_Obj *next = a;
	
	while (next->next != NULL)
	  next = next->next;
	
	next->next = b;
	b->prev = next;
     }
   return a;
}

     
   

void slrn_free_mime_error(Slrn_Mime_Error_Obj *obj) /*{{{*/
{
   Slrn_Mime_Error_Obj *tmp;

   while (obj != NULL)
     {
	tmp = obj->next;
	if (obj->err_str != NULL)
	  slrn_free(obj->err_str);
	if (obj->msg != NULL)
	  slrn_free (obj->msg);

	slrn_free((char *) obj);
	obj=tmp;
     }
}

/*}}}*/

int slrn_mime_process_article (Slrn_Article_Type *a)/*{{{*/
{
   if (a == NULL)
     return -1;

   if (a->mime.was_parsed)
     return 0;

   a->mime.was_parsed = 1;	       /* or will be */
   
   rfc1522_decode_headers (a);

/* Is there a reason to use the following line? */
/*   if (NULL == find_header_line (a, "Mime-Version:")) return;*/
/*   if ((-1 == parse_content_type_line (a))
       || (-1 == parse_content_transfer_encoding_line (a)))*/
   if (-1 == parse_content_type_line (a))
     {
	a->mime.needs_metamail = 1;
	return 0;
     }

   switch (parse_content_transfer_encoding_line (a))
     {
      case ENCODED_RAW:
	/*return 0;*/
	/* Now falls through to the identity encoding. */

      case ENCODED_7BIT:
      case ENCODED_8BIT:
      case ENCODED_BINARY:
	/* Already done. */
	break;
	
      case ENCODED_BASE64:
	decode_mime_base64 (a);
	break;
	
      case ENCODED_QUOTED:
	if (-1 == decode_mime_quoted_printable (a))
	  return -1;
	break;
	
      default:
	a->mime.needs_metamail = 1;
	return 0;
     }
   
   if ((a->mime.needs_metamail == 0) &&
	     (a->mime.charset == NULL))
     {
	a->mime.charset = slrn_safe_strmalloc("us-ascii");
	return 0;
     }
 
   if ((a->mime.needs_metamail == 0) &&
	(slrn_case_strncmp("us-ascii",
			   a->mime.charset,8) != 0) &&
	(slrn_case_strcmp(Slrn_Display_Charset,
			   a->mime.charset) != 0))
     {
	if (-1 == slrn_convert_article(a, Slrn_Display_Charset, a->mime.charset))
	  {
	  }
     }
   return 0;
}


/*}}}*/

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

int slrn_mime_call_metamail (void)/*{{{*/
{
#ifdef VMS
   return 0;
#else
   int init = Slrn_TT_Initialized;
   char tempfile [SLRN_MAX_PATH_LEN];
   Slrn_Article_Line_Type *ptr;
   FILE *fp;
   char *tmp, *mm, *cmd;
   
   if (NULL == Slrn_Current_Article)
     return -1;
   ptr = Slrn_Current_Article->lines;
     
   if ((Slrn_Use_Meta_Mail == 0)
       || Slrn_Batch
       || (slrn_get_yesno (1, _("Process this MIME article with metamail")) <= 0))
     return 0;

# if defined(__os2__) || defined(__NT__)
   if (NULL == (tmp = getenv ("TMP")))
     tmp = ".";
# else
   tmp = "/tmp";
# endif
   
   fp = slrn_open_tmpfile_in_dir (tmp, tempfile, sizeof (tempfile));

   if (fp == NULL)
     {
	slrn_error (_("Unable to open tmp file for metamail."));
	return 0;
     }
   
   while (ptr) 
     {
	fputs(ptr->buf, fp);
	putc('\n', fp);
	ptr = ptr->next;
     }
   slrn_fclose(fp);

   mm = Slrn_MetaMail_Cmd;
   
   if ((mm == NULL)
       || (*mm == 0)
       || (strlen (mm) > SLRN_MAX_PATH_LEN))
     mm = "metamail";

   cmd = slrn_strdup_strcat (mm, " ", tempfile, NULL);
   
   /* Make sure that metamail has a normal environment */
   slrn_set_display_state (0);
   
   slrn_posix_system(cmd, 0);
   slrn_delete_file (tempfile);
   slrn_free (cmd);
   
   printf(_("Press return to continue ..."));
   getchar();
   fflush(stdin); /* get rid of any pending input! */
   
   slrn_set_display_state (init);
   return 1;
#endif  /* NOT VMS */
}

/*}}}*/


/* -------------------------------------------------------------------------
 * MIME encoding routines.
 * -------------------------------------------------------------------------*/

/* expexts a->cline pointing to the last headerline, and the body in a->raw_lines */
Slrn_Mime_Error_Obj *slrn_mime_encode_article(Slrn_Article_Type *a, int *hibin, char *from_charset) /*{{{*/
{
  char *charset = Slrn_Outgoing_Charset;
  char *charset_end=NULL;
  Slrn_Article_Line_Type *endofheader, *rline=a->raw_lines;
   
  a->cline->next=(Slrn_Article_Line_Type *) slrn_malloc(sizeof(Slrn_Article_Line_Type),1,1);
  a->cline->next->prev=a->cline;
  a->cline=a->cline->next;
  a->cline->flags=HEADER_LINE;
  a->cline->buf=slrn_safe_strmalloc("Mime-Version: 1.0");
  endofheader = a->cline;
  
  if (*hibin == -1)
    {
       *hibin = 0;
       while(rline != NULL)
	 {
	    if (slrn_string_nonascii(rline->buf))
	      {
		 *hibin = 1;
		 break;
	      }
	    rline=rline->next;
	 }
       rline=a->raw_lines;
    }

  if (*hibin)
    {
       do
	 {
	    if ((charset_end=slrn_strbyte(charset, ',')) != NULL)
	      {
		 *charset_end = '\0';
	      }

	    if (slrn_case_strcmp(charset, from_charset) == 0)
	    /* No recoding needed */
	      {
		 while(rline != NULL)
		   {
		      a->cline->next=rline;
		      a->cline->next->prev=a->cline;
		      a->cline=a->cline->next;
		      rline=rline->next;
		   }
		 a->raw_lines=NULL;
		 break;
	      }

	    if ( slrn_test_convert_article(a, charset, from_charset)  == 0)
	      {
		 break;
	      }
	    if (charset_end != NULL)
	      {
		 *charset_end=',';
		 charset = charset_end + 1;
	      }
	 } while(charset_end != NULL);
       if (endofheader == a->cline)
	 {
	    /* if we get here, no encoding was possible*/
	    return slrn_add_mime_error(NULL, _("Can't determine suitable charset for body"), NULL, -1 , MIME_ERROR_CRIT);
	 }
    }
  else
    {
       a->cline->next=(Slrn_Article_Line_Type *) slrn_malloc(sizeof(Slrn_Article_Line_Type),1,1);
       a->cline->next->prev=a->cline;
       a->cline=a->cline->next;
       a->cline->flags=HEADER_LINE;
       a->cline->buf=slrn_safe_strmalloc("Content-Type: text/plain; charset=us-ascii");
       a->cline->next=(Slrn_Article_Line_Type *) slrn_malloc(sizeof(Slrn_Article_Line_Type),1,1);
       a->cline->next->prev=a->cline;
       a->cline=a->cline->next;
       a->cline->flags=HEADER_LINE;
       a->cline->buf=slrn_safe_strmalloc("Content-Transfer-Encoding: 7bit");

       while(rline != NULL)
	 {
	    a->cline->next=rline;
	    a->cline->next->prev=a->cline;
	    a->cline=a->cline->next;
	    rline=rline->next;
	 }
       a->raw_lines=NULL;
       return NULL;
    }
  /* if we get here, the posting contains 8bit chars */
  a->cline = endofheader;
  
  endofheader = (Slrn_Article_Line_Type *) slrn_malloc(sizeof(Slrn_Article_Line_Type),1,1);
  endofheader->flags=HEADER_LINE;
  endofheader->buf=slrn_strdup_printf ("Content-Type: text/plain; charset=%s", charset);
  
  endofheader->next = (Slrn_Article_Line_Type *) slrn_malloc(sizeof(Slrn_Article_Line_Type),1,1);
  endofheader->next->flags=HEADER_LINE;
  endofheader->next->buf=slrn_safe_strmalloc("Content-Transfer-Encoding: 8bit");
  endofheader->next->next = a->cline->next;
  a->cline->next->prev= endofheader->next;
  endofheader->prev = a->cline;
  a->cline->next = endofheader;
  
  if (charset_end != NULL) *charset_end=',';
  
  return NULL;
}

/*}}}*/

static Slrn_Mime_Error_Obj *fold_line (char **s_ptr)/*{{{*/
{
   int fold=0, pos=0, last_ws=0;
   char *s=*s_ptr;
   char *tmp, *ret;
   
   if (strlen(s) <= 78)
     {
	/* nothing to do */
	return NULL;
     }
	
   /* First step: counting. */
   
   /* skip the first word and all ws after it
    * (folding after the keyword is not allowed) */
   /* I'm not sure about that FS */
   while (((last_ws) || (s[pos] != ' ')) && (s[pos] != '\0'))
     {
	if (s[pos] == ' ')
	     last_ws = pos;
	pos++;
     }
   if (pos > 77)
     return slrn_add_mime_error(NULL, _("First word of header is too long."), *s_ptr, 0,MIME_ERROR_WARN);

   do
     {
	if ((s[pos] == ' ') || (s[pos] == '\0'))
	  {
	     if (pos > 77)
	       {
		  if (last_ws == 0)
		    return slrn_add_mime_error(NULL, _("One word of the header is too long to get folded."), *s_ptr, 0,MIME_ERROR_WARN);
		  fold++;
		  s += last_ws;
		  pos=0;
	       }
	     last_ws=pos;
	  }
     }
   while (s[pos++] != '\0');
  
   /* Second step: Folding */
   s=*s_ptr;
   ret=tmp=slrn_safe_malloc(strlen(s)+1+fold);
   last_ws=pos=0;
   do
     {
	if ((s[pos] == ' ') || (s[pos] == '\0'))
	  {
	     if (pos > 77)
	       {
		  strncpy(tmp, s, last_ws);
		  tmp+=last_ws;
		  *(tmp++)='\n';
		  s+=last_ws;
		  pos=0;
	       }
	     last_ws=pos;
	  }
     }
   while (s[pos++] != '\0');

   strncpy(tmp, s, strlen(s)+1);

   slrn_free(*s_ptr);
   *s_ptr=ret;
   return NULL;
}
/*}}}*/

/* Do the actual encoding.
 * Note: This function does not generate encoded-words that are longer
 *       than max_len chars. The line folding is performed by
 *       a separate function. */
static Slrn_Mime_Error_Obj *encode_string (char **s_ptr, unsigned int offset,/*{{{*/
			  unsigned int len, char *from_charset, unsigned int max_len, unsigned int *chars_more)
{
   unsigned int extralen[2];
   char *s=*s_ptr + offset;
   char *charset= Slrn_Outgoing_Charset;
   char *charset_end=NULL;
   char *ret, *tmp=NULL;
   unsigned int i;

   extralen[0] = extralen[1] = 0;

   /* Loop through the list of character sets converting the substring */
  do
    {
       if ((charset_end=slrn_strbyte(charset, ',')) != NULL)
	 {
	    *charset_end = '\0';
	 }
     
       if (!slrn_case_strcmp(charset, from_charset))
       /* No recoding needed */
	 {
	    tmp=*s_ptr;
	    extralen[0] =0;
	    break;
	 }

       if (NULL != (tmp = slrn_convert_substring(*s_ptr, offset, len, charset, from_charset, 1)))
	 {
	    extralen[0] = strlen(tmp) - strlen(*s_ptr);
	    slrn_free(*s_ptr);
	    *s_ptr=tmp;
	    s=*s_ptr + offset;

	    break;
	 }
       if (charset_end != NULL)
	 {
	    *charset_end=',';
	    charset = charset_end + 1;
	 }
    } while(charset_end != NULL);
  if (tmp == NULL)
    {
       /* if we get here, no encoding was possible*/
       return slrn_add_mime_error(NULL, _("Can't find suitable charset for Header"), *s_ptr, 0 ,MIME_ERROR_CRIT);
    }
   extralen[1] = strlen(charset) + 2+3+2;
   
   for (i=0; i < len + extralen[0]; i++)
     {
	if ( *s & 0x80)
	  {
	     extralen[1] +=  2;
	  }
	s++;
     }
   if ((max_len) && (max_len < len + extralen[0] + extralen[1]))
     {
	return slrn_mime_error (_("One word in the header is too long after encoding."), *s_ptr, 0, MIME_ERROR_CRIT);
     }
   ret=tmp=slrn_safe_malloc(strlen(*s_ptr) +1 +extralen[1]);
   strncpy(ret, *s_ptr, offset);
   tmp=ret + offset;
   sprintf(tmp, "=?%s?Q?", charset); /* safe */
   tmp=tmp + strlen(charset) + 5;

   if (charset_end != NULL) *charset_end=',';

   s= *s_ptr + offset;
   for (i=0; i< len + extralen[0]; i++)
     {
	unsigned char ch;
	if ((ch =*s) & 0x80)
	  {
	     sprintf (tmp, "=%02X", (int) ch); /* safe */
	     tmp+=3;
	  }
	else
	  {
	     if (ch == ' ') *tmp = '_';
	     else *tmp = ch;
	     tmp++;
	  }
	s++;
     }
   *tmp++ = '?';
   *tmp++ = '=';
   strncpy(tmp, s, strlen(s)+1); /* safe */
    
   
   slrn_free(*s_ptr);
   *s_ptr=ret;
   
   *chars_more = extralen[0] + extralen[1];
   return NULL;
}
/*}}}*/

/* This function encodes a header, i.e.,  HeaderName: value.... */
/* Try to cause minimal overhead when encoding. */
static Slrn_Mime_Error_Obj *min_encode (char **s_ptr, char *from_charset) /*{{{*/
{
  char *s=*s_ptr;
   unsigned int pos;
   unsigned int encode_pos;
   unsigned int extralen;
   unsigned int encode_len;
   unsigned int max_pos;
   int encode = 0;
  Slrn_Mime_Error_Obj *ret;

   max_pos = strlen (s);
   while (max_pos > 0)
     {
	max_pos--;
	if (s[max_pos] & 0x80)
	  {
	     max_pos++;
	     break;
	  }
     }

   encode_pos = 0;
   encode_len = 75;
   pos = 0;

   while ((pos < max_pos) || encode)
     {
	if ((s[pos] == ' ') || (s[pos] == '\0') || (s[pos] == '\n'))
	  {
	    if (encode)
	      {
		 ret = encode_string(s_ptr, encode_pos, pos - encode_pos, from_charset,encode_len, &extralen);
		 if (ret != NULL)
		   return ret;
		 pos += extralen;
		 max_pos += extralen;
		 s = *s_ptr;
		 encode = 0;
	      }
	     while ((s[pos] == ' ') || (s[pos] == '\n'))
	       pos++;
	     encode_pos = pos;
	     continue;
	 }

	if (s[pos] & 0x80)
	  encode = 1;

       pos++;
    }
  return NULL;
}/*}}}*/

/* Encode structured header fields ("From:", "To:", "Cc:" and such) {{{ */

#define TYPE_ADD_ONLY 1
#define TYPE_OLD_STYLE 2
#define TYPE_RFC_2882 3

/* What is RFC 2882???? */
#define RFC_2882_NOT_ATOM_CHARS "(),.:;<>@[\\]\""
#define RFC_2882_NOT_DOTATOM_CHARS "(),:;<>@[\\]\""
#define RFC_2882_NOT_QUOTED_CHARS "\t\\\""
#define RFC_2882_NOT_DOMLIT_CHARS "[\\]"
#define RFC_2882_NOT_COMMENT_CHARS "(\\)"

static Slrn_Mime_Error_Obj *encode_comment (char **s_ptr, char *from_charset, unsigned int *start, unsigned int *max) /*{{{*/
{
   char *s =*s_ptr;
   unsigned int pos = *start;
   unsigned int encode_pos = 0;
   int encode=0;
   unsigned int extralen;
   Slrn_Mime_Error_Obj *err;

   while (pos < *max)
     {
	if ((s[pos]== ' ') || (s[pos]== '(') || (s[pos] == ')'))
	  {
	     if (encode)
	       {
		  err = encode_string(s_ptr, encode_pos, pos-encode_pos, from_charset, 78, &extralen);
		  if (err != NULL)
		    return err;
		  *max += extralen;
		  pos +=extralen;
		  s=*s_ptr;
		  encode=0;
	       }
	     if (s[pos] == ')')
	       {
		  *start = pos;
		  return NULL;
	       }
	     if (s[pos] == '(')
	       {
		  pos++;
		  if ((err= encode_comment(s_ptr, from_charset, &pos, max)) != NULL)
		    return err;
		  s = *s_ptr;
	       }
	     pos++;
	     encode_pos = pos;
	     continue;
	  }

	if (s[pos] & 0x80)
	  {
	     encode=1;
	     pos++;
	     continue;
	  }

	if (NULL != slrn_strbyte(RFC_2882_NOT_COMMENT_CHARS, s[pos]))
	  {
	     return slrn_mime_error (_("Illegal char in displayname of address header."), *s_ptr, 0, MIME_ERROR_CRIT);
	  }
	pos++;
     }
   /* upps*/
   return slrn_mime_error (_("Comment opened but never closed in address header."), *s_ptr, 0, MIME_ERROR_CRIT);
}

/*}}}*/

static Slrn_Mime_Error_Obj *encode_phrase (char **s_ptr, char *from_charset, unsigned int *start, unsigned int *max) /*{{{*/
{
   char *s =*s_ptr;
   unsigned int encode_pos = 0;
   unsigned int pos = *start;
   unsigned int extralen;
   int in_quote=0;
   int encode=0;
   Slrn_Mime_Error_Obj *err;

   while (pos < *max)
     {
	if ((s[pos]== ' ') || (s[pos]== '"'))
	  {
	     if (encode)
	       {
		  if ( (err= encode_string(s_ptr, encode_pos, pos-encode_pos, from_charset, 78, &extralen)) != NULL)
		    {
		       return err;
		    }
		  *max += extralen;
		  pos +=extralen;
		  s=*s_ptr;
		  encode=0;
	       }
	     if (s[pos]== '"')
	       {
		  if (in_quote)
		    in_quote=0;
		  else
		    in_quote=1;
	       }
	     pos++;
	     encode_pos = pos;
	     continue;
	  }

	if (s[pos] & 0x80)
	  {
	     encode = 1;
	     pos++;
	     continue;
	  }

	if (!in_quote)
	  {
	     if (s[pos]== '(')
	       {
		  if (encode)
		    {
		       if ( (err= encode_string(s_ptr,encode_pos, pos-encode_pos, from_charset, 78, &extralen)) != NULL)
			 {
			    return err;
			 }
		       *max += extralen;
		       pos += extralen;
		       s=*s_ptr;
		       encode=0;
		    }
		  pos++;
		  if (NULL != (err = encode_comment(s_ptr, from_charset, &pos, max)))
		    return err;
		  s = *s_ptr;
		  pos++;
		  encode_pos = pos;
		  continue;
	       }

	     if ((s[pos-1] == ' ') && (s[pos] == '<'))
	       /* Address begins, return */
	       {
		  *start= pos;
		  return NULL;
	       }
	     if (NULL != slrn_strbyte(RFC_2882_NOT_ATOM_CHARS, s[pos]))
	       {
		  return slrn_mime_error (_("Illegal char in displayname of address header."), *s_ptr, 0, MIME_ERROR_CRIT);
	       }
	  }
	else
	  {
	     if (NULL != slrn_strbyte(RFC_2882_NOT_QUOTED_CHARS, s[pos]))
	       {
		  return slrn_mime_error (_("Illegal char in quoted displayname of address header."), *s_ptr, 0, MIME_ERROR_CRIT);
	       }
	  }
	pos++;
     }
   /* never reached */
   return NULL;
}

/*}}}*/

/* It will return with *start set to the position of smax, or to the
 * of the '@', '>' (type == TYPE_RFC_2882), or space character, if present.
 */
static Slrn_Mime_Error_Obj *encode_localpart (char **s_ptr, char *from_charset, unsigned int *start, unsigned int max, int type) /*{{{*/
{
   char *s, *str, *smax;
   int in_quote=0;
   unsigned int nchars;

   str = *s_ptr;
   s = str + (*start);
   smax = str + max;
   
   nchars = 0;
   while (s < smax)
     {
	char ch = *s++;
	
	if (ch & 0x80)
	  return slrn_mime_error (_("Non 7-bit char in localpart of address header."), str, 0, MIME_ERROR_CRIT);

	if (ch == '"')
	  {
	     in_quote = !in_quote;
	     if (in_quote)
	       continue;

	     if (s < smax)
	       {
		  ch = *s;
		  if ((ch == '@') || (ch == '>') || (ch == ' '))
		    continue;	       /* process these next time through */
	       }
	     return slrn_mime_error (_("Wrong quote in localpart of address header."), str, 0, MIME_ERROR_CRIT);
	  }
	
	if (in_quote)
	  {
	     if ((ch == ' ')
		 || (NULL != slrn_strbyte(RFC_2882_NOT_QUOTED_CHARS, ch)))
	       return slrn_mime_error (_("Illegal char in quoted localpart of address header."), str, 0, MIME_ERROR_CRIT);
	     
	     continue;
	  }

	if (nchars 
	    && ((ch == '@')
		|| (ch == ' ')
		|| ((ch == '>') && (type = TYPE_RFC_2882))))
	  {
	     s--;
	     break;
	  }

	if ((ch == ' ')
	    || (NULL != slrn_strbyte(RFC_2882_NOT_DOTATOM_CHARS, ch)))
	  return slrn_mime_error (_("Illegal char in localpart of address header."), str, 0, MIME_ERROR_CRIT);

	nchars++;
     }

   if (in_quote)
     return slrn_mime_error (_("Wrong quote in localpart of address header."), str, 0, MIME_ERROR_CRIT);

   if (nchars == 0)
     return slrn_mime_error (_("localpart of the address header is empty"), str, 0, MIME_ERROR_CRIT);

   *start=(s-str);
   /* return slrn_mime_error (_("No domain found in address header."), *s_ptr, 0, MIME_ERROR_CRIT); */
   return NULL;
}

/*}}}*/

static Slrn_Mime_Error_Obj *encode_domain (char **s_ptr, char *from_charset, unsigned int *start, unsigned int max, int type) /*{{{*/
{
   char *s =*s_ptr;
   unsigned int pos = *start;

   while (pos < max)
     {
#ifndef HAVE_LIBIDN
	if (s[pos] & 0x80)
	  {
	     /* TODO: encode with libidn */
	     return slrn_mime_error (_("Non 7-bit char in domain of address header. libidn is not yet supported."), *s_ptr, 0, MIME_ERROR_CRIT);
	  }
#endif
	if (type == TYPE_RFC_2882)
	  {
	     if (s[pos] == '>')
	       {
		  *start=pos;
		  return NULL;
	       }
	
	     if (s[pos] == ' ')
	       {
		  return slrn_mime_error (_("Space in domain."), *s_ptr, 0, MIME_ERROR_CRIT);
	       }
	  }
	else
	  {
	     if (s[pos] == ' ')
	       {
		  *start=pos;
		  return NULL;
	       }
	  }
	if (NULL != slrn_strbyte(RFC_2882_NOT_DOTATOM_CHARS, s[pos]))
	  {
	     return slrn_mime_error (_("Illegal char in domain of address header."), *s_ptr, 0, MIME_ERROR_CRIT);
	  }
	pos++;
	
     }
   *start=pos;
   return NULL;
}

/*}}}*/

static Slrn_Mime_Error_Obj *encode_domainlit (char **s_ptr, char *from_charset, unsigned int *start, unsigned int max) /*{{{*/
{
   char *s =*s_ptr;
   unsigned int pos = *start;

   while (pos < max)
     {
#ifndef HAVE_LIBIDN
	if (s[pos] & 0x80)
	  {
	     return slrn_mime_error (_("Non 7-bit char in domain of address header. libidn is not yet supported."), *s_ptr, 0, MIME_ERROR_CRIT);
	  }
#endif
	if (s[pos] == ']')
	  {
	     *start=pos;
	     return NULL;
	  }
	if (NULL != slrn_strbyte(RFC_2882_NOT_DOMLIT_CHARS, s[pos]))
	  {
	     return slrn_mime_error (_("Illegal char in domain-literal of address header."), *s_ptr, 0, MIME_ERROR_CRIT);
	  }
	pos++;
     }
   return slrn_mime_error (_("domain-literal opened but never closed."), *s_ptr, 0, MIME_ERROR_CRIT);
}

/*}}}*/

/* The encodes a comma separated list of addresses.  Each item in the list
 * is assumed to be of the following forms:
 * 
 *    address (Comment-text)
 *    address (Comment-text)
 *    Comment-text <address>
 * 
 * Here address is user@domain, user@[domain], or user.
 */
static Slrn_Mime_Error_Obj *from_encode (char **s_ptr, char *from_charset) /*{{{*/
{
   unsigned int head_start=0, head_end;
   int type=0;
   unsigned int pos=0;
   int in_quote=0;
   char *s=*s_ptr;
   Slrn_Mime_Error_Obj *err;
   char ch;
   
   while ((0 != (ch = s[head_start]))
	  && (ch != ':'))
     head_start++;

   if (ch != ':')
     return slrn_mime_error (_("A colon is missing from the address header"), *s_ptr, 0, MIME_ERROR_CRIT);
   
   head_start++;		       /* skip colon */

   while (head_start < strlen(s))      /* s may change in the loop */
     {
	/* skip past leading whitespace */
	while ((0 != (ch = s[head_start]))
	       && ((ch == ' ') || (ch == '\t')))
	  head_start++;

	if (ch == 0)
	  break;
	  
	/* If multiple addresses are given, split at ',' */
	head_end=head_start;
	in_quote=0;
	type=TYPE_ADD_ONLY;

	while (1)
	  {
	     ch = s[head_end];
	     if (ch == 0)
	       break;
	     
	     if (in_quote)
	       {
		  if (ch == '"')
		    in_quote = !in_quote;

		  head_end++;
		  continue;
	       }
	     
	     if (ch == '<')
	       {
		  type = TYPE_RFC_2882;
		  head_end++;
		  continue;
	       }
	     
	     if (ch == ',')
	       break;
	     
	     head_end++;
	  }

	if (in_quote)
	  {
	     return slrn_mime_error (_("Quote opened but never closed in address header."), *s_ptr, 0, MIME_ERROR_CRIT);
	  }

	pos=head_start;
	s=*s_ptr;
	if (type == TYPE_RFC_2882)     /* foo <bar> */
	  {
	     if (s[pos] != '<')
	       {
		  /* phrase <bar> */
		  if ((err=encode_phrase(s_ptr, from_charset, &pos, &head_end)) != NULL)
		    {
		       return err;
		    }
	       }
	     s = *s_ptr;
	     /* at this point, pos should be at '<' */
	     if (s[pos] != '<')
	       {
		  return slrn_mime_error (_("Address appear to have a misplaced '<'."),
					  *s_ptr, 0, MIME_ERROR_CRIT);
	       }
	     pos++;
	  }

	if (NULL != (err = encode_localpart(s_ptr, from_charset, &pos, head_end, type)))
	  return err;

	s = *s_ptr;
	if (s[pos] == '@')
	  {
	     pos++;
	     if (s[pos] == '[')
	       {
		  pos++;
		  err=encode_domainlit(s_ptr, from_charset, &pos, head_end);
		  if (err != NULL)
		    return err;
		  pos++;	       /* skip ']' */
	       }
	     else
	       {
		  err = encode_domain (s_ptr, from_charset, &pos, head_end, type);
		  if (err != NULL)
		    return err;
	       }
	  }

	s=*s_ptr;
	if (type == TYPE_RFC_2882)
	  {
	     if (s[pos] != '>')
	       {
		  return slrn_mime_error (_("Expected closing '>' character in the address"), 
					  *s_ptr, 0, MIME_ERROR_CRIT);
	       }
	     pos++;
	  }

	/* after domainpart only (folding) Whitespace and comments are allowed*/
	while (pos < head_end)
	  {
	     ch = s[pos++];
	     
	     if ((ch == ' ') || (ch == '\n') || (ch == '\t'))
	       continue;

	     if (ch == '(')
	       {
		  err = encode_comment(s_ptr, from_charset, &pos, &head_end);
		  if (err != NULL)
		    return err;

		  s=*s_ptr;
		  pos++;	       /* skip ')' */
		  continue;
	       }
	     return slrn_mime_error (_("Illegal char after domain of address header."), *s_ptr, 0, MIME_ERROR_CRIT);
	     pos++;
	  } /* while (pos < head_end) */
	
	/* head_end should be at ',', so skip over it. */
	head_start=head_end+1;

     } /*while (head_start < strlen(*s_ptr)) */
   return NULL;
}
/*}}}*/

/*}}}*/

Slrn_Mime_Error_Obj *slrn_mime_header_encode (char **s_ptr, char *from_charset) /*{{{*/
{
   char *s=*s_ptr;
   Slrn_Mime_Error_Obj *ret=NULL;

   
   /* preserve 8bit characters in those headers */
   if (!slrn_case_strncmp ( s,
			    "Newsgroups: ", 12) ||
       !slrn_case_strncmp ( s,
			    "Followup-To: ", 13))
     return NULL; /* folding?*/
   
   if (!slrn_case_strncmp ( s,
			    "From: ", 6) ||
       !slrn_case_strncmp ( s,
			    "Cc: ", 4) ||
       !slrn_case_strncmp ( s,
			    "To: ", 4))
     ret = from_encode (s_ptr, from_charset);
   else if (!slrn_case_strncmp ( s,
				 "Mail-Copies-To: ", 16))
     {
	char *b = slrn_skip_whitespace (s + 16);
	if ((!slrn_case_strncmp(b,  "nobody", 6) ||
	     !slrn_case_strncmp(b,  "poster", 6)) &&
	    (0 == *slrn_skip_whitespace(b+6)))
	  return NULL; /* nothing to convert */
	ret = from_encode (s_ptr, from_charset);
     }   
   else
     {
	if (slrn_string_nonascii(s))
	  {
	     ret = min_encode (s_ptr, from_charset);
	  }
     }
   if (ret != NULL)
	return ret;

   return fold_line(s_ptr);
}

/*}}}*/

#endif /* NOT SLRNPULL_CODE */
