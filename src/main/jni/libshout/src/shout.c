/* -*- c-basic-offset: 8; -*- */
/* shout.c: Implementation of public libshout interface shout.h
 *
 *  Copyright (C) 2002-2004 the Icecast team <team@icecast.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: shout.c 11554 2006-06-09 23:09:33Z brendan $
 */

#ifdef HAVE_CONFIG_H
 #include <shout/config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <shout/shout.h>
#include <net/sock.h>
#include "timing/timing.h"
#include "httpp/httpp.h"

#include "shout_private.h"
#include "util.h"
#include <jni.h>
#include <android/log.h>
#define LOG_TAG "SHOUT"
#define LOGD(format, args...) do{__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "[%s:%d] -> "format, __FUNCTION__, __LINE__, ##args);}while(0);


/* -- local prototypes -- */
static int queue_data(shout_queue_t *queue, const unsigned char *data, size_t len);
static int queue_str(shout_t *self, const char *str);
static int queue_printf(shout_t *self, const char *fmt, ...);
static inline void queue_free(shout_queue_t *queue);
static int send_queue(shout_t *self);
static int get_response(shout_t *self);
static int try_connect (shout_t *self);
static int try_write (shout_t *self, const void *data, size_t len);

static int create_request(shout_t *self);
static int create_http_request(shout_t *self);
static int create_xaudiocast_request(shout_t *self);
static int create_icy_request(shout_t *self);
static int parse_response(shout_t *self);
static int parse_http_response(shout_t *self);
static int parse_xaudiocast_response(shout_t *self);

static char *http_basic_authorization(shout_t *self);

/* -- static data -- */
static int _initialized = 0;

/* -- public functions -- */

void shout_init(void)
{
	if (_initialized)
		return;

	sock_initialize();
	_initialized = 1;
}

void shout_shutdown(void)
{
	if (!_initialized)
		return;

	sock_shutdown();
	_initialized = 0;
}

shout_t *shout_new(int samplerate, int bitrate, int channels)
{
	shout_t *self;

	/* in case users haven't done this explicitly. Should we error
	 * if not initialized instead? */
	LOGD("Init_Icecast_Clientaaaaaaaaaaaa");
	shout_init();
	LOGD("Init_Icecast_Clientbbbbbbbbbbbb");
	if (!(self = (shout_t *)calloc(1, sizeof(shout_t)))) {
		LOGD("Init_Icecast_Clientccccccccccccccc");
		return NULL;
	}
	LOGD("Init_Icecast_Client111111111111111");
	if (shout_set_host(self, LIBSHOUT_DEFAULT_HOST) != SHOUTERR_SUCCESS) {
		LOGD("Init_Icecast_Clientddddddddddddd");
		shout_free(self);

		return NULL;
	}
	LOGD("Init_Icecast_Client222222222222");
	if (shout_set_user(self, LIBSHOUT_DEFAULT_USER) != SHOUTERR_SUCCESS) {
		LOGD("Init_Icecast_Clienteeeeeeeeeeeeeeeeeee");
		shout_free(self);

		return NULL;
	}
	LOGD("Init_Icecast_Client3333333333333333");
	if (shout_set_agent(self, LIBSHOUT_DEFAULT_USERAGENT) != SHOUTERR_SUCCESS) {
		LOGD("Init_Icecast_Clientfffffffffffffffff");
		shout_free(self);

		return NULL;
	}
	LOGD("Init_Icecast_Client444444444444444444");
	if (!(self->audio_info = _shout_util_dict_new())) {
		LOGD("Init_Icecast_Clientgggggggggggggggggg");
		shout_free(self);

		return NULL;
	}
	shout_set_audio_infoint(self, "ice-samplerate", samplerate);
	shout_set_audio_infoint(self, "ice-bitrate", bitrate);
	shout_set_audio_infoint(self, "ice-channels", channels);
		
	LOGD("Init_Icecast_Client555555555555555555");
	self->port = LIBSHOUT_DEFAULT_PORT;
	self->format = LIBSHOUT_DEFAULT_FORMAT;
	self->protocol = LIBSHOUT_DEFAULT_PROTOCOL;

    self->interval = 0;
    self->since_meta_block = 0;
    self->metadata_len = 17;
    self->metadata = (char*)calloc(1, self->metadata_len);
    if (NULL == self->metadata)
        return NULL;
    strcpy(self->metadata, "\001StreamTitle='';");
    self->metadata_update = 1;
    thread_mutex_create(&(self->metadata_mutex));

	return self;
}

void shout_free(shout_t *self)
{
	if (!self) return;

	if (self->host) free(self->host);
	if (self->password) free(self->password);
	if (self->mount) free(self->mount);
	if (self->auth) free(self->auth);
	if (self->name) free(self->name);
	if (self->url) free(self->url);
	if (self->genre) free(self->genre);
	if (self->description) free(self->description);
	if (self->user) free(self->user);
	if (self->useragent) free(self->useragent);
    if (self->metadata) free(self->metadata);
	if (self->audio_info) _shout_util_dict_free (self->audio_info);

    thread_mutex_destroy(&(self->metadata_mutex));

	free(self);
}

int shout_open(shout_t *self)
{

	/* sanity check */
	if (!self)
	{

		return SHOUTERR_INSANE;
	}
	if (self->state != SHOUT_STATE_UNCONNECTED)
	{

		return SHOUTERR_CONNECTED;
	}
	if (!self->host || !self->password || !self->port)
	{

		return self->error = SHOUTERR_INSANE;
	}
	if (self->format == SHOUT_FORMAT_OGG && self->protocol != SHOUT_PROTOCOL_HTTP)
	{

		return self->error = SHOUTERR_UNSUPPORTED;
	}
	LOGD("Init_Icecast_Client before try_connect");
	return self->error = try_connect(self);
	/*return self->error = sock_try_connection(self);*/
}


int shout_close(shout_t *self)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state == SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_UNCONNECTED;

	if (self->state == SHOUT_STATE_CONNECTED && self->close)
		self->close(self);

	sock_close(self->socket);
	self->state = SHOUT_STATE_UNCONNECTED;
	self->starttime = 0;
	self->senttime = 0;
	queue_free(&self->rqueue);
	queue_free(&self->wqueue);

	return self->error = SHOUTERR_SUCCESS;
}

int shout_send(shout_t *self, const unsigned char *data, size_t len)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_CONNECTED)
		return self->error = SHOUTERR_UNCONNECTED;

	if (self->starttime <= 0)
		self->starttime = timing_get_time();

	if (!len)
		return send_queue(self);

	return self->send(self, data, len);
}
ssize_t shout_send_data(shout_t *self, const unsigned char *data, size_t len)
{
	ssize_t ret;

	if (!self) 
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_CONNECTED)
		return SHOUTERR_UNCONNECTED;

	self->error = SHOUTERR_SUCCESS;

	/* send immediately if possible (should be the common case) */
	if (len && ! self->wqueue.len) {
		if ((ret = try_write(self, data, len)) < 0)
			return self->error;
		if (ret < len) {
			self->error = queue_data(&self->wqueue, data + ret, len - ret);
			if (self->error != SHOUTERR_SUCCESS)
				return self->error;
		}

		return len;
	}

	self->error = queue_data(&self->wqueue, data, len);
	if (self->error != SHOUTERR_SUCCESS)
		return self->error;

	ret = send_queue(self);
	if (ret == SHOUTERR_SUCCESS || (len && ret == SHOUTERR_BUSY))
		return len;

	return ret;
}

ssize_t shout_send_meta(shout_t *self)
{
    ssize_t ret;
    thread_mutex_lock(&(self->metadata_mutex));

    LOGD("Get Lock");

    if (self->metadata_update == 1){
        ret = shout_send_data(self, self->metadata, self->metadata_len);
        LOGD("update new meta len is %d content is %s", self->metadata_len, self->metadata);
        if (ret != self->metadata_len)
            ret = -1;
        else
            ret = 0;
        self->metadata_update = 0;
    }
    else{
        ret = shout_send_data(self, "\0", 1);
        if (ret != 1)
            ret = -1;
        else
            ret = 0;
    }
    if (self->metadata_update == 2)
        self->metadata_update = 1;
    self->since_meta_block = 0;
    thread_mutex_unlock(&(self->metadata_mutex));

    LOGD("Release Lock");

    return ret;
}

ssize_t shout_send_raw(shout_t *self, const unsigned char *data, size_t len)
{
    ssize_t ret = 0;
    ssize_t written = 0;

    if (self->interval){
        unsigned int remaining = self->interval - self->since_meta_block;
        if (remaining <= len)
        {
            /* send any mp3 before the metadata block */
            if (remaining)
            {
                ret = shout_send_data(self, data, remaining);
                self->since_meta_block += ret;
                written += ret;
            }

            // send meta data
            ret = shout_send_meta(self);
            if (ret != 0)
                return -1;
            
            data += remaining;
            len -= remaining;
        }
    }
    if (len > 0) {
        ret = shout_send_data(self, data, len);
        self->since_meta_block += ret;
        written += ret;
    }

    return written;
}

ssize_t shout_queuelen(shout_t *self)
{
	if (!self)
		return SHOUTERR_INSANE;

	return (ssize_t)self->wqueue.len;
}


void shout_sync(shout_t *self)
{
	int64_t sleep;

	if (!self)
		return;

	if (self->senttime == 0)
		return;

	sleep = self->senttime / 1000 - (timing_get_time() - self->starttime);
	if (sleep > 0)
		timing_sleep((uint64_t)sleep);
		
}

int shout_delay(shout_t *self)
{

	if (!self)
		return 0;

	if (self->senttime == 0)
		return 0;

	/* Is this cast to double needed? */
	return self->senttime / 1000 - (timing_get_time() - self->starttime);
}
  
shout_metadata_t *shout_metadata_new(void)
{
	return _shout_util_dict_new();
}

void shout_metadata_free(shout_metadata_t *self)
{
	if (!self)
		return;

	_shout_util_dict_free(self);
}

int shout_metadata_add(shout_metadata_t *self, const char *name, const char *value)
{
	if (!self || !name)
		return SHOUTERR_INSANE;

	return _shout_util_dict_set(self, name, value);
}

/* open second socket to server, send HTTP request to change metadata.
 * TODO: prettier error-handling. */
int shout_set_metadata(shout_t *self, shout_metadata_t *metadata)
{
	sock_t socket;
	int rv;
	char *encvalue;

	if (!self || !metadata)
		return SHOUTERR_INSANE;

	if (!(encvalue = _shout_util_dict_urlencode(metadata, '&')))
		return SHOUTERR_MALLOC;

	if ((socket = sock_connect(self->host, self->port)) <= 0)
		return SHOUTERR_NOCONNECT;

	if (self->protocol == SHOUT_PROTOCOL_ICY)
		rv = sock_write(socket, "GET /admin.cgi?mode=updinfo&pass=%s&%s HTTP/1.0\r\nUser-Agent: %s (Mozilla compatible)\r\n\r\n",
		  self->password, encvalue, shout_get_agent(self));
	else if (self->protocol == SHOUT_PROTOCOL_HTTP) {
		char *auth = http_basic_authorization(self);

		rv = sock_write(socket, "GET /admin/metadata?mode=updinfo&mount=%s&%s HTTP/1.0\r\nUser-Agent: %s\r\n%s\r\n",
		  self->mount, encvalue, shout_get_agent(self), auth ? auth : "");
                free(auth);
	} else
		rv = sock_write(socket, "GET /admin.cgi?mode=updinfo&pass=%s&mount=%s&%s HTTP/1.0\r\nUser-Agent: %s\r\n\r\n",
		  self->password, self->mount, encvalue, shout_get_agent(self));
	free(encvalue);
	if (!rv) {
		sock_close(socket);
		return SHOUTERR_SOCKET;
	}

	sock_close(socket);

	return SHOUTERR_SUCCESS;
}

/* getters/setters */
const char *shout_version(int *major, int *minor, int *patch)
{
	if (major)
		*major = LIBSHOUT_MAJOR;
	if (minor)
		*minor = LIBSHOUT_MINOR;
	if (patch)
		*patch = LIBSHOUT_MICRO;

	return VERSION;
}

int shout_get_errno(shout_t *self)
{
	return self->error;
}

const char *shout_get_error(shout_t *self)
{
	if (!self)
		return "Invalid shout_t";

	switch (self->error) {
	case SHOUTERR_SUCCESS:
		return "No error";
	case SHOUTERR_INSANE:
		return "Nonsensical arguments";
	case SHOUTERR_NOCONNECT:
		return "Couldn't connect";
	case SHOUTERR_NOLOGIN:
		return "Login failed";
	case SHOUTERR_SOCKET:
		return "Socket error";
	case SHOUTERR_MALLOC:
		return "Out of memory";
	case SHOUTERR_CONNECTED:
		return "Cannot set parameter while connected";
	case SHOUTERR_UNCONNECTED:
		return "Not connected";
        case SHOUTERR_BUSY:
                return "Socket is busy";
	case SHOUTERR_UNSUPPORTED:
		return "This libshout doesn't support the requested option";
	default:
		return "Unknown error";
	}
}

/* Returns:
 *   SHOUTERR_CONNECTED if the connection is open,
 *   SHOUTERR_UNCONNECTED if it has not yet been opened,
 *   or an error from try_connect, including SHOUTERR_BUSY
 */
int shout_get_connected(shout_t *self)
{
	int rc;

	if (!self)
		return SHOUTERR_INSANE;

	if (self->state == SHOUT_STATE_CONNECTED)
		return SHOUTERR_CONNECTED;
	if (self->state != SHOUT_STATE_UNCONNECTED) {
		if ((rc = try_connect(self)) == SHOUTERR_SUCCESS)
			return SHOUTERR_CONNECTED;
		return rc;
	}

	return SHOUTERR_UNCONNECTED;
}

int shout_set_host(shout_t *self, const char *host)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	if (self->host)
		free(self->host);

	if (!(self->host = _shout_util_strdup(host)))
		return self->error = SHOUTERR_MALLOC;

	return self->error = SHOUTERR_SUCCESS;
}

const char *shout_get_host(shout_t *self)
{
	if (!self)
		return NULL;

	return self->host;
}

int shout_set_port(shout_t *self, unsigned short port)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	self->port = port;

	return self->error = SHOUTERR_SUCCESS;
}

unsigned short shout_get_port(shout_t *self)
{
	if (!self)
		return 0;

	return self->port;
}

int shout_set_password(shout_t *self, const char *password)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	if (self->password)
		free (self->password);

	if (!(self->password = _shout_util_strdup(password)))
		return self->error = SHOUTERR_MALLOC;

	return self->error = SHOUTERR_SUCCESS;
}

const char* shout_get_password(shout_t *self)
{
	if (!self)
		return NULL;

	return self->password;
}

int shout_set_mount(shout_t *self, const char *mount)
{
	size_t len;

	if (!self || !mount)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;
	
	if (self->mount)
		free(self->mount);

	len = strlen (mount) + 1;
	if (mount[0] != '/')
		len++;

	if (!(self->mount = malloc(len)))
		return self->error = SHOUTERR_MALLOC;

	sprintf (self->mount, "%s%s", mount[0] == '/' ? "" : "/", mount);

	return self->error = SHOUTERR_SUCCESS;
}

const char *shout_get_mount(shout_t *self)
{
	if (!self)
		return NULL;

	return self->mount;
}

int shout_set_name(shout_t *self, const char *name)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	if (self->name)
		free(self->name);

	if (!(self->name = _shout_util_strdup(name)))
		return self->error = SHOUTERR_MALLOC;

	return self->error = SHOUTERR_SUCCESS;
}

const char *shout_get_name(shout_t *self)
{
	if (!self)
		return NULL;

	return self->name;
}

int shout_set_url(shout_t *self, const char *url)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	if (self->url)
		free(self->url);

	if (!(self->url = _shout_util_strdup(url)))
		return self->error = SHOUTERR_MALLOC;

	return self->error = SHOUTERR_SUCCESS;
}

const char *shout_get_url(shout_t *self)
{
	if (!self)
		return NULL;

	return self->url;
}

int shout_set_genre(shout_t *self, const char *genre)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	if (self->genre)
		free(self->genre);

	if (! (self->genre = _shout_util_strdup (genre)))
		return self->error = SHOUTERR_MALLOC;

	return self->error = SHOUTERR_SUCCESS;
}

const char *shout_get_genre(shout_t *self)
{
	if (!self)
		return NULL;

	return self->genre;
}

int shout_set_agent(shout_t *self, const char *agent)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	if (self->useragent)
		free(self->useragent);

	if (! (self->useragent = _shout_util_strdup (agent)))
		return self->error = SHOUTERR_MALLOC;

	return self->error = SHOUTERR_SUCCESS;
}

const char *shout_get_agent(shout_t *self)
{
	if (!self)
		return NULL;

	return self->useragent;
}


int shout_set_user(shout_t *self, const char *username)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	if (self->user)
		free(self->user);

	if (! (self->user = _shout_util_strdup (username)))
		return self->error = SHOUTERR_MALLOC;

	return self->error = SHOUTERR_SUCCESS;
}

const char *shout_get_user(shout_t *self)
{
	if (!self)
		return NULL;

	return self->user;
}

int shout_set_description(shout_t *self, const char *description)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	if (self->description)
		free(self->description);

	if (! (self->description = _shout_util_strdup (description)))
		return self->error = SHOUTERR_MALLOC;

	return self->error = SHOUTERR_SUCCESS;
}

const char *shout_get_description(shout_t *self)
{
	if (!self)
		return NULL;

	return self->description;
}

int shout_set_dumpfile(shout_t *self, const char *dumpfile)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return SHOUTERR_CONNECTED;

	if (self->dumpfile)
		free(self->dumpfile);

	if (! (self->dumpfile = _shout_util_strdup (dumpfile)))
		return self->error = SHOUTERR_MALLOC;

	return self->error = SHOUTERR_SUCCESS;
}

const char *shout_get_dumpfile(shout_t *self)
{
	if (!self)
		return NULL;

	return self->dumpfile;
}

int shout_set_audio_info(shout_t *self, const char *name, const char *value)
{
	return self->error = _shout_util_dict_set(self->audio_info, name, value);
}

int shout_set_audio_infoint(shout_t *self, const char *name, int value)
{
	char buf[10];
	_int_to_charp(value, buf, 10);
	return self->error = _shout_util_dict_set(self->audio_info, name, buf);
}

const char *shout_get_audio_info(shout_t *self, const char *name)
{
	return _shout_util_dict_get(self->audio_info, name);
}

int shout_set_public(shout_t *self, unsigned int public)
{
	if (!self || (public != 0 && public != 1))
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	self->public = public;

	return self->error = SHOUTERR_SUCCESS;
}

unsigned int shout_get_public(shout_t *self)
{
	if (!self)
		return 0;

	return self->public;
}

int shout_set_format(shout_t *self, unsigned int format)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	if (format != SHOUT_FORMAT_OGG && format != SHOUT_FORMAT_MP3)
		return self->error = SHOUTERR_UNSUPPORTED;

	self->format = format;

	return self->error = SHOUTERR_SUCCESS;
}

unsigned int shout_get_format(shout_t* self)
{
	if (!self)
		return 0;

	return self->format;
}

int shout_set_protocol(shout_t *self, unsigned int protocol)
{
	if (!self)
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	if (protocol != SHOUT_PROTOCOL_HTTP &&
	    protocol != SHOUT_PROTOCOL_XAUDIOCAST &&
	    protocol != SHOUT_PROTOCOL_ICY)
		return self->error = SHOUTERR_UNSUPPORTED;

	self->protocol = protocol;

	return self->error = SHOUTERR_SUCCESS;
}

unsigned int shout_get_protocol(shout_t *self)
{
	if (!self)
		return 0;

	return self->protocol;
}

int shout_set_nonblocking(shout_t *self, unsigned int nonblocking)
{
	if (!self || (nonblocking != 0 && nonblocking != 1))
		return SHOUTERR_INSANE;

	if (self->state != SHOUT_STATE_UNCONNECTED)
		return self->error = SHOUTERR_CONNECTED;

	self->nonblocking = nonblocking;

	return SHOUTERR_SUCCESS;
}

unsigned int shout_get_nonblocking(shout_t *self)
{
	if (!self)
		return 0;

	return self->nonblocking;
}

/* -- static function definitions -- */

/* queue data in pages of SHOUT_BUFSIZE bytes */
static int queue_data(shout_queue_t *queue, const unsigned char *data, size_t len)
{
	shout_buf_t *buf;
	size_t plen;

	if (!len)
		return SHOUTERR_SUCCESS;

	if (!queue->len) {
		queue->head = calloc(1, sizeof (shout_buf_t));
		if (! queue->head)
			return SHOUTERR_MALLOC;
	}

	for (buf = queue->head; buf->next; buf = buf->next);

	/* Maybe any added data should be freed if we hit a malloc error?
	 * Otherwise it'd be impossible to tell where to start requeueing.
	 * (As if anyone ever tried to recover from a malloc error.) */
	while (len > 0) {
		if (buf->len == SHOUT_BUFSIZE) {
			buf->next = calloc(1, sizeof (shout_buf_t));
			if (! buf->next)
				return SHOUTERR_MALLOC;
			buf->next->prev = buf;
			buf = buf->next;
		}

		plen = len > SHOUT_BUFSIZE - buf->len ? SHOUT_BUFSIZE - buf->len : len;
		memcpy (buf->data + buf->len, data, plen);
		buf->len += plen;
		data += plen;
		len -= plen;
		queue->len += plen;
	}

	return SHOUTERR_SUCCESS;
}

static inline int queue_str(shout_t *self, const char *str)
{
	return queue_data(&self->wqueue, (const unsigned char*)str, strlen(str));
}

/* this should be shared with sock_write. Create libicecommon. */
static int queue_printf(shout_t *self, const char *fmt, ...)
{
	char buffer[1024];
	char *buf;
	va_list ap, ap_retry;
	int len;

	buf = buffer;

	va_start(ap, fmt);
	va_copy(ap_retry, ap);

	len = vsnprintf(buf, sizeof(buffer), fmt, ap);

	self->error = SHOUTERR_SUCCESS;
	if (len > 0) {
		if ((size_t)len < sizeof(buffer))
			queue_data(&self->wqueue, (unsigned char*)buf, len);
		else {
			buf = malloc(++len);
			if (buf) {
				len = vsnprintf(buf, len, fmt, ap_retry);
				queue_data(&self->wqueue, (unsigned char*)buf, len);
				free(buf);
			} else
				self->error = SHOUTERR_MALLOC;
		}
	}

	va_end(ap_retry);
	va_end(ap);

	return self->error;
}

static inline void queue_free(shout_queue_t *queue)
{
	shout_buf_t *prev;

	while (queue->head) {
		prev = queue->head;
		queue->head = queue->head->next;
		free(prev);
	}
	queue->len = 0;
}

static int get_response(shout_t *self)
{
	char buf[1024];
	int rc, blen;
	char *pc;
	shout_buf_t *queue;
	int newlines = 0;

	rc = sock_read_bytes(self->socket, buf, sizeof(buf));

	if (rc < 0 && sock_recoverable(sock_error()))
		return SHOUTERR_BUSY;
	if (rc <= 0)
		return SHOUTERR_SOCKET;

	if ((rc = queue_data(&self->rqueue, (unsigned char*)buf, rc)))
		return rc;

	/* work from the back looking for \r?\n\r?\n. Anything else means more
	 * is coming. */
	for (queue = self->rqueue.head; queue->next; queue = queue->next);
	pc = (char*)queue->data + queue->len - 1;
	blen = queue->len;
	while (blen) {
		if (*pc == '\n')
			newlines++;
		/* we may have to scan the entire queue if we got a response with
		 * data after the head line (this can happen with eg 401) */
		else if (*pc != '\r')
			newlines = 0;

		if (newlines == 2)
			return SHOUTERR_SUCCESS;

		blen--;
		pc--;

		if (!blen && queue->prev) {
			queue = queue->prev;
			pc = (char*)queue->data + queue->len - 1;
			blen = queue->len;
		}
	}

	return SHOUTERR_BUSY;
}

static int try_connect (shout_t *self)
{
	int rc;
	int port;
	LOGD("Init_Icecast_Client in try_connect:%d",self->state);
	/* the breaks between cases are omitted intentionally */
	switch (self->state) {
	case SHOUT_STATE_UNCONNECTED:
		LOGD("Init_Icecast_Client 11111111111111111");
		port = self->port;
		if (shout_get_protocol(self) == SHOUT_PROTOCOL_ICY)
		{

			port++;
		}
		LOGD("Init_Icecast_Client 11111111111111111");
		if (shout_get_nonblocking(self)) {
			LOGD("Init_Icecast_Client before sock_connect_non_blocking");
			if ((self->socket = sock_connect_non_blocking(self->host, port)) < 0)
			{

				return self->error = SHOUTERR_NOCONNECT;
			}
			self->state = SHOUT_STATE_CONNECT_PENDING;

		} else {
			LOGD("Init_Icecast_Client 11111111111111111");
			if ((self->socket = sock_connect(self->host, port)) < 0)
			{
				LOGD("Init_Icecast_Client sock_connect<0");
				return self->error = SHOUTERR_NOCONNECT;
			}
			if ((rc = create_request(self)) != SHOUTERR_SUCCESS)
			{

				return rc;
			}
			self->state = SHOUT_STATE_REQ_PENDING;

		}

	case SHOUT_STATE_CONNECT_PENDING:

		if (shout_get_nonblocking(self)) {
			if ((rc = sock_connected(self->socket, 0)) < 1) {
				if (rc == SOCK_ERROR) {
                                        rc = SHOUTERR_SOCKET;
                                        goto failure;
				} else
					return SHOUTERR_BUSY;
			}
			if ((rc = create_request(self)) != SHOUTERR_SUCCESS)
                                goto failure;
		}
		self->state = SHOUT_STATE_REQ_PENDING;

	case SHOUT_STATE_REQ_PENDING:

		do
			rc = send_queue(self);
		while (!shout_get_nonblocking(self) && rc == SHOUTERR_BUSY);
                if (rc == SHOUTERR_BUSY)
                        return rc;
		if (rc != SHOUTERR_SUCCESS)
                        goto failure;
		self->state = SHOUT_STATE_RESP_PENDING;

	case SHOUT_STATE_RESP_PENDING:

		do
			rc = get_response(self);
		while (!shout_get_nonblocking(self) && rc == SHOUTERR_BUSY);
                if (rc == SHOUTERR_BUSY)
                        return rc;

		if (rc != SHOUTERR_SUCCESS)
                        goto failure;

		if ((rc = parse_response(self)) != SHOUTERR_SUCCESS)
                        goto failure;

		if (self->format == SHOUT_FORMAT_OGG) {
			if ((rc = self->error = shout_open_ogg(self)) != SHOUTERR_SUCCESS)
                                goto failure;
		} else if (self->format == SHOUT_FORMAT_MP3) {
			if ((rc = self->error = shout_open_mp3(self)) != SHOUTERR_SUCCESS)
                                goto failure;
		} else {
                        rc = SHOUTERR_INSANE;
                        goto failure;
		}

	case SHOUT_STATE_CONNECTED:

		self->state = SHOUT_STATE_CONNECTED;
	}

	return SHOUTERR_SUCCESS;

failure:

        shout_close(self);
	return rc;
}

static int try_write (shout_t *self, const void *data, size_t len)
{
    int ret;
    size_t pos = 0;

    /* loop until whole buffer is written (unless it would block) */
    do {
        ret = sock_write_bytes (self->socket, data + pos, len - pos);
        if (ret > 0)
            pos += ret;
    } while (pos < len && ret >= 0);

    if (ret < 0)
    {
        if (sock_recoverable (sock_error()))
        {
            self->error = SHOUTERR_BUSY;
            return pos;
        }
        self->error = SHOUTERR_SOCKET;
        return ret;
    }

    return pos;
}

/* collect nodes of a queue into a single buffer */
static int collect_queue(shout_buf_t *queue, char **buf)
{
	shout_buf_t *node;
	int pos = 0;
	int len = 0;

	for (node = queue; node; node = node->next)
		len += node->len;

	if (!(*buf = malloc(len)))
		return SHOUTERR_MALLOC;

	for (node = queue; node; node = node->next) {
		memcpy(*buf + pos, node->data, node->len);
		pos += node->len;
	}

	return len;
}

static int send_queue(shout_t *self)
{
	shout_buf_t *buf;
	int ret;

	if (!self->wqueue.len)
		return SHOUTERR_SUCCESS;

	buf = self->wqueue.head;
	
	LOGD("send buf is %s", buf->data);
	
	while (buf) {
		ret = try_write (self, buf->data + buf->pos, buf->len - buf->pos);
		if (ret < 0)
			return self->error;

		buf->pos += ret;
		self->wqueue.len -= ret;
		if (buf->pos == buf->len) {
			self->wqueue.head = buf->next;
			free(buf);
			buf = self->wqueue.head;
			if (buf)
				buf->prev = NULL;
		} else /* incomplete write */
			return SHOUTERR_BUSY;
	}

	return self->error = SHOUTERR_SUCCESS;
}

static int create_request(shout_t *self)
{
	if (self->protocol == SHOUT_PROTOCOL_HTTP)
		return create_http_request(self);
	else if (self->protocol == SHOUT_PROTOCOL_XAUDIOCAST)
		return create_xaudiocast_request(self);
	else if (self->protocol == SHOUT_PROTOCOL_ICY)
		return create_icy_request(self);

	return self->error = SHOUTERR_UNSUPPORTED;
}


static char* _get_dict_join(util_dict *dict, char delim)
{
	char *res, *tmp;
	char *enc;
	int start = 1;

	for (res = NULL; dict; dict = dict->next) {
		/* encode key */
		if (!dict->key)
			continue;

		if (start) {
			if (!(res = malloc(strlen(dict->key) + 1))) {
				return NULL;
			}
			sprintf(res, "%s", dict->key);

			start = 0;
		} else {
			if (!(tmp = realloc(res, strlen(res) + strlen(dict->key) + 2))) {
				free(res);
				return NULL;
			} else
				res = tmp;
			sprintf(res + strlen(res), "%c%s", delim, dict->key);
		}

		/* encode value */
		if (!dict->val)
			continue;

		if (!(tmp = realloc(res, strlen(res) + strlen(dict->val) + 2))) {
			free(res);
			return NULL;
		} else
			res = tmp;
		sprintf(res + strlen(res), "=%s", dict->val);
	}

	return res;

}
static int create_http_request(shout_t *self)
{
	char *auth;
	char *ai;
	int ret = SHOUTERR_MALLOC;

	/* this is lazy code that relies on the only error from queue_* being
	 * SHOUTERR_MALLOC */
	do {
		if (queue_printf(self, "SOURCE %s HTTP/1.0\r\n", self->mount))
			break;
		if (self->password) {
			if (! (auth = http_basic_authorization(self)))
				break;
			if (queue_str(self, auth)) {
				free(auth);
				break;
			}
			free(auth);
		}
		if (self->useragent && queue_printf(self, "User-Agent: %s\r\n", self->useragent))
			break;
		if (self->format == SHOUT_FORMAT_OGG && queue_printf(self, "Content-Type: application/ogg\r\n"))
			break;
		if (self->format == SHOUT_FORMAT_MP3 && queue_printf(self, "Content-Type: audio/mpeg\r\n"))
			break;
		if (queue_printf(self, "ice-name: %s\r\n", self->name ? self->name : "no name"))
			break;
		if (queue_printf(self, "ice-public: %d\r\n", self->public))
			break;
        if (self->interval && queue_printf(self, "icy-metaint: %d\r\n", self->interval))
            break;

		if (self->url && queue_printf(self, "ice-url: %s\r\n", self->url))
			break;
		if (self->genre && queue_printf(self, "ice-genre: %s\r\n", self->genre))
			break;

		if ((ai = _get_dict_join(self->audio_info, ';'))) {

			LOGD("autdio_info buf is %s", ai);

			if (queue_printf(self, "ice-audio-info: %s\r\n", ai)) {
				free(ai);
				break;
			}
			free(ai);
		}
		if (self->description && queue_printf(self, "ice-description: %s\r\n", self->description))
			break;
		if (queue_str(self, "\r\n"))
			break;
		
		ret = SHOUTERR_SUCCESS;
	} while (0);

	return ret;
}

static char *http_basic_authorization(shout_t *self)
{
	char *out, *in;
	int len;

	if (!self || !self->user || !self->password)
		return NULL;

	len = strlen(self->user) + strlen(self->password) + 2;
	if (!(in = malloc(len)))
		return NULL;
	sprintf(in, "%s:%s", self->user, self->password);
	out = _shout_util_base64_encode(in);
	free(in);

	len = strlen(out) + 24;
	if (!(in = malloc(len))) {
		free(out);
		return NULL;
	}
	sprintf(in, "Authorization: Basic %s\r\n", out);
	free(out);
	
	return in;
}

static int parse_response(shout_t *self)
{
	if (self->protocol == SHOUT_PROTOCOL_HTTP)
		return parse_http_response(self);
	else if (self->protocol == SHOUT_PROTOCOL_XAUDIOCAST ||
		 self->protocol == SHOUT_PROTOCOL_ICY)
		return parse_xaudiocast_response(self);

	return self->error = SHOUTERR_UNSUPPORTED;
}

static int parse_http_response(shout_t *self)
{
	http_parser_t *parser;
	char *header = NULL;
	int hlen = 0;
	int code;
	char *retcode;
#if 0
	char *realm;
#endif

	/* all this copying! */
	hlen = collect_queue(self->rqueue.head, &header);
	if (hlen <= 0)
		return SHOUTERR_MALLOC;
	queue_free(&self->rqueue);

	parser = httpp_create_parser();
	httpp_initialize(parser, NULL);
	if (httpp_parse_response(parser, header, hlen, self->mount)) {
		retcode = httpp_getvar(parser, HTTPP_VAR_ERROR_CODE);
		code = atoi(retcode);
		if(code >= 200 && code < 300) {
			httpp_destroy(parser);
			free (header);
			return SHOUTERR_SUCCESS;
		}
	}

	free(header);
	httpp_destroy(parser);
	return self->error = SHOUTERR_NOLOGIN;
}

static int create_xaudiocast_request(shout_t *self)
{
	const char *bitrate;
	int ret;

	bitrate = shout_get_audio_info(self, SHOUT_AI_BITRATE);
	if (!bitrate)
		bitrate = "0";

	ret = SHOUTERR_MALLOC;
	do {
		if (queue_printf(self, "SOURCE %s %s\n", self->password, self->mount))
			break;
		if (queue_printf(self, "x-audiocast-name: %s\n", self->name ? self->name : "unnamed"))
			break;
		if (queue_printf(self, "x-audiocast-url: %s\n", self->url ? self->url : "http://www.icecast.org/"))
			break;
		if (queue_printf(self, "x-audiocast-genre: %s\n", self->genre ? self->genre : "icecast"))
			break;
		if (queue_printf(self, "x-audiocast-bitrate: %s\n", bitrate))
			break;
		if (queue_printf(self, "x-audiocast-public: %i\n", self->public))
			break;
		if (queue_printf(self, "x-audiocast-description: %s\n", self->description ? self->description : "Broadcasting with the icecast streaming media server!"))
			break;
		if (self->dumpfile && queue_printf(self, "x-audiocast-dumpfile: %s\n", self->dumpfile))
			break;
		if (queue_str(self, "\n"))
			break;

		ret = SHOUTERR_SUCCESS;
	} while (0);
		
	return ret;
}

static int parse_xaudiocast_response(shout_t *self)
{
	char *response;

	if (collect_queue(self->rqueue.head, &response) <= 0)
		return SHOUTERR_MALLOC;
	queue_free(&self->rqueue);

	if (!strstr(response, "OK")) {
		free(response);
		return SHOUTERR_NOLOGIN;
	}
	free(response);

	return SHOUTERR_SUCCESS;
}

static int create_icy_request(shout_t *self)
{
	const char *bitrate;
	int ret;

	bitrate = shout_get_audio_info(self, SHOUT_AI_BITRATE);
	if (!bitrate)
		bitrate = "0";

	ret = SHOUTERR_MALLOC;
	do {
		if (queue_printf(self, "%s\n", self->password))
			break;
		if (queue_printf(self, "icy-name:%s\n", self->name ? self->name : "unnamed"))
			break;
		if (queue_printf(self, "icy-url:%s\n", self->url ? self->url : "http://www.icecast.org/"))
			break;
		if (queue_str(self, "icy-irc:\nicy-aim:\nicy-icq:\n"))
			break;
		if (queue_printf(self, "icy-pub:%i\n", self->public))
			break;
		if (queue_printf(self, "icy-genre:%s\n", self->genre ? self->genre : "icecast"))
			break;
		if (queue_printf(self, "icy-br:%s\n\n", bitrate))
			break;

		ret = SHOUTERR_SUCCESS;
	} while (0);

	return ret;
}
int shout_set_auth(shout_t *self, const char *auth)
{
	size_t len;
	
	if (!self || !auth)
		return SHOUTERR_INSANE;

	if (self->auth)
		free(self->auth);

	len = strlen (auth) + 1;

	if (!(self->auth = malloc(len)))
		return SHOUTERR_MALLOC;

	sprintf (self->auth, "%s", auth);

	return SHOUTERR_SUCCESS;

}
int shout_set_vid(shout_t *self, int vid)
{
	if (!self)
		return SHOUTERR_INSANE;

	self->vid = vid;
	return 0;
}

int shout_upate_sts(shout_t *self, const char* sts)
{
	int ret = 0;
	char buf[2048];
	sock_t socket = sock_connect(self->host, self->port);
	if (SOCK_ERROR == socket)
		return SHOUTERR_SOCKET;

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 2048, "GET /admin/updatestatus?mount=%s&vid=%d&status=%s HTTP/1.1\r\nAuthorization: %s\r\n\r\n", 
		self->mount, self->vid, sts, self->auth);
	LOGD("build update sts request is: %s", buf);

	ret = sock_write_bytes(socket, buf, strlen(buf));
	if (ret != strlen(buf)){
		LOGD("send update request len is: %d", ret);
		sock_close(socket);
		return SHOUTERR_SOCKET;
	}

	memset(buf, 0, sizeof(buf));
	ret = sock_read_bytes(socket, buf, 2048);
	LOGD("recv response is: %s", buf);
	sock_close(socket);
	
	if (strstr(buf, "HTTP/1.0 200 OK") != NULL)
		return SHOUTERR_SUCCESS;
	else
		return SHOUTERR_SOCKET;
}

int shout_set_interval(shout_t *self, int interval)
{
    if (!self)
        return SHOUTERR_INSANE;

    self->interval = interval;
    return 0;
}
int shout_update_title(shout_t *self, const char* title)
{
    const char streamtitle[] = "StreamTitle='";
    size_t size;
    unsigned char len_byte;
    unsigned int len = sizeof(streamtitle) + 2; /* the StreamTitle, quotes, ; and null */
    char *p = NULL;

    LOGD("update title: %s", title);
    if (!self || !title)
        return SHOUTERR_INSANE;
    
    len += strlen(title);

    /* work out the metadata len byte */
    len_byte = (len-1) / 16 + 1;

    /* now we know how much space to allocate, +1 for the len byte */
    size = len_byte * 16 + 1;

    p = calloc(1, size);
    if (NULL == p)
        return SHOUTERR_MALLOC;

    snprintf (p, size, "%c%s%s';", len_byte, streamtitle, title);

    thread_mutex_lock(&(self->metadata_mutex));
    LOGD("Get lock, new meta len is %d, content is %s",size, p);

    if (self->metadata)
        free(self->metadata);
    self->metadata = p;
    self->metadata_len = size;
    self->metadata_update = 2;

    thread_mutex_unlock(&(self->metadata_mutex));
    LOGD("Release lock");
    
    return SHOUTERR_SUCCESS;
}
