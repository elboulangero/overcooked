/*
 * Goodvibes Radio Player
 *
 * Copyright (C) 2015-2021 Arnaud Rebillout
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * This code is based on / inspired by the following:
 * - Parole
 * - Totem (Playlist Parser)
 * Big thanks to them.
 *
 * A definitive guide to playlist formats (a must-read, though I didn't):
 * http://gonze.com/playlists/playlist-format-survey.html
 */

#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <libsoup/soup.h>

#include "base/glib-object-additions.h"
#include "base/gv-base.h"

#include "core/gv-playlist.h"

// WISHED Test with a lot, really a lot of different stations.
//        I'm pretty sure this playlist implementation is not robust enough.

// TODO   Validate URI, send an error message if it's invalid ?
//        But then, shouldn't that be done in GvStation instead ? Or not ?

/*
 * Properties
 */

enum {
	/* Reserved */
	PROP_0,
	/* Properties */
	PROP_URI,
	PROP_STREAM_LIST,
	/* Number of properties */
	PROP_N
};

static GParamSpec *properties[PROP_N];

/*
 * Signals
 */

enum {
	SIGNAL_DOWNLOADED,
	/* Number of signals */
	SIGNAL_N
};

static guint signals[SIGNAL_N];

/*
 * GObject definitions
 */

struct _GvPlaylistPrivate {
	gchar             *uri;
	GvPlaylistFormat format;
	GSList           *streams;
};

typedef struct _GvPlaylistPrivate GvPlaylistPrivate;

struct _GvPlaylist {
	/* Parent instance structure */
	GObject parent_instance;
	/* Private data */
	GvPlaylistPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE(GvPlaylist, gv_playlist, G_TYPE_OBJECT)

/*
 * Helpers
 */

typedef GSList *(*PlaylistParser) (const gchar *, gsize);

/* Parse a M3U playlist, which is a simple text file,
 * each line being an uri.
 * https://en.wikipedia.org/wiki/M3U
 */

static GSList *
parse_playlist_m3u(const gchar *text, gsize text_size G_GNUC_UNUSED)
{
	GSList *list = NULL;
	const gchar *eol;
	gchar **lines;
	guint i;

	/* Guess which delimiter is used to separate each lines.
	 * Traditionnaly, UNIX would only use `\n`, Windows would use `\r\n`.
	 */
	if (strstr(text, "\r\n"))
		eol = "\r\n";
	else
		eol = "\n";

	/* Split into different lines */
	lines = g_strsplit(text, eol, -1);
	if (lines == NULL) {
		WARNING("Empty m3u playlist");
		return NULL;
	}

	/* Iterate on lines */
	for (i = 0; lines[i] != NULL; i++) {
		gchar *line = lines[i];

		/* Remove leading & trailing whitespaces */
		line = g_strstrip(line);

		/* Ignore emtpy lines and comments */
		if (line[0] == '\0' || line[0] == '#')
			continue;

		/* If it's not an URI, we discard it */
		if (!strstr(line, "://"))
			continue;

		/* Add to stream list */
		list = g_slist_append(list, g_strdup(line));
	}

	g_strfreev(lines);
	return list;
}

/* Parse a PLS playlist, which is a "Desktop Entry File" in the Unix world,
 * or an "INI File" in the windows realm.
 * https://en.wikipedia.org/wiki/PLS_(file_format)
 */

static guint
pls_get_n_items(GKeyFile *keyfile)
{
	const gchar *keys[] = { "NumberOfEntries", "numberofentries",
				"NumberOfEvents", "numberofevents",
				NULL };
	const gchar **ptr;
	const gchar *key = NULL;
	GError *err = NULL;
	gint n_items;

	for (ptr = keys; *ptr; ptr++) {
		if (g_key_file_has_key(keyfile, "playlist", *ptr, NULL)) {
			key = *ptr;
			break;
		}
	}

	if (key == NULL) {
		WARNING("Failed to get the number of items in pls playlist");
		return 0;
	}

	n_items = g_key_file_get_integer(keyfile, "playlist", key, &err);
	if (err) {
		WARNING("Failed to get key '%s': %s", key, err->message);
		g_error_free(err);
		return 0;
	}

	return n_items;
}

static GSList *
parse_playlist_pls(const gchar *text, gsize text_size)
{
	GSList *list = NULL;
	GKeyFile *keyfile;
	GError *err = NULL;
	guint i, n_items;

	keyfile = g_key_file_new();

	/* Parse the file */
	g_key_file_load_from_data(keyfile, text, text_size, 0, &err);
	if (err) {
		WARNING("Failed to parse pls playlist: %s", err->message);
		g_error_free(err);
		goto end;
	}

	/* Get the number of items */
	n_items = pls_get_n_items(keyfile);
	if (n_items == 0)
		goto end;

	/* Get all stream uris */
	for (i = 0; i < n_items; i++) {
		gchar key[8];
		gchar *str;

		g_snprintf(key, sizeof key, "File%u", i + 1);

		str = g_key_file_get_string(keyfile, "playlist", key, &err);
		if (err) {
			WARNING("Failed to get '%s': %s", key, err->message);
			g_error_free(err);
			err = NULL;
			continue;
		}

		/* Add to stream list.
		 * No need to duplicate str, it's already an allocated string.
		 */
		list = g_slist_append(list, str);
	}

end:
	g_key_file_free(keyfile);

	return list;
}

/* Parse an ASX (Advanced Stream Redirector) playlist.
 * https://en.wikipedia.org/wiki/Advanced_Stream_Redirector
 */

static void
asx_parse_element_cb(GMarkupParseContext *context G_GNUC_UNUSED,
                     const gchar         *element_name,
                     const gchar        **attribute_names,
                     const gchar        **attribute_values,
                     gpointer             user_data,
                     GError             **err G_GNUC_UNUSED)
{
	GSList **llink = (GSList **) user_data;
	const gchar *href;
	guint i;

	/* We're only interested in the 'ref' element */
	if (g_ascii_strcasecmp(element_name, "ref"))
		return;

	/* Get 'href' attribute */
	href = NULL;
	for (i = 0; attribute_names[i]; i++) {
		if (!g_ascii_strcasecmp(attribute_names[i], "href")) {
			href = attribute_values[i];
			break;
		}
	}

	/* Add to stream list */
	if (href)
		*llink = g_slist_append(*llink, g_strdup(href));
}

static void
asx_error_cb(GMarkupParseContext *context G_GNUC_UNUSED,
             GError              *err   G_GNUC_UNUSED,
             gpointer             user_data)
{
	GSList **llink = (GSList **) user_data;

	g_slist_free_full(*llink, g_free);
	*llink = NULL;
}

static GSList *
parse_playlist_asx(const gchar *text, gsize text_size)
{
	GMarkupParseContext *context;
	GMarkupParser parser = {
		asx_parse_element_cb,
		NULL,
		NULL,
		NULL,
		asx_error_cb,
	};
	GError *err = NULL;
	GSList *list = NULL;

	context = g_markup_parse_context_new(&parser, 0, &list, NULL);

	if (!g_markup_parse_context_parse(context, text, text_size, &err)) {
		WARNING("Failed to parse context: %s", err->message);
		g_error_free(err);
	}

	g_markup_parse_context_free(context);

	return list;
}

/* Parse an XSPF (XML Shareable Playlist Format) playlist.
 * https://en.wikipedia.org/wiki/XML_Shareable_Playlist_Format
 */

static void
xspf_text_cb(GMarkupParseContext  *context,
             const gchar          *text,
             gsize                 text_len G_GNUC_UNUSED,
             gpointer              user_data,
             GError              **err G_GNUC_UNUSED)
{
	GSList **llink = (GSList **) user_data;
	const gchar *element_name;

	element_name = g_markup_parse_context_get_element(context);

	/* We're only interested in the 'location' element */
	if (g_ascii_strcasecmp(element_name, "location"))
		return;

	/* Add to stream list */
	*llink = g_slist_append(*llink, g_strdup(text));
}

static void
xspf_error_cb(GMarkupParseContext *context G_GNUC_UNUSED,
              GError              *err   G_GNUC_UNUSED,
              gpointer             user_data)
{
	GSList **llink = (GSList **) user_data;

	g_slist_free_full(*llink, g_free);
	*llink = NULL;
}

static GSList *
parse_playlist_xspf(const gchar *text, gsize text_size)
{
	GMarkupParseContext *context;
	GMarkupParser parser = {
		NULL,
		NULL,
		xspf_text_cb,
		NULL,
		xspf_error_cb,
	};
	GError *err = NULL;
	GSList *list = NULL;

	context = g_markup_parse_context_new(&parser, 0, &list, NULL);

	if (!g_markup_parse_context_parse(context, text, text_size, &err)) {
		WARNING("Failed to parse context: %s", err->message);
		g_error_free(err);
	}

	g_markup_parse_context_free(context);

	return list;
}

/*
 * Signal handlers & callbacks
 */

static void
on_message_completed(SoupSession *session,
                     SoupMessage *msg,
                     GvPlaylist *self)
{
	GvPlaylistPrivate *priv = self->priv;
	PlaylistParser parser;
	GSList *item;

	TRACE("%p, %p, %p", session, msg, self);

	/* Check the response */
	if (SOUP_STATUS_IS_SUCCESSFUL(msg->status_code) == FALSE) {
		WARNING("Failed to download playlist (%u): %s",	msg->status_code, msg->reason_phrase);
		if (!g_strcmp0(soup_status_get_phrase(msg->status_code), "SSL handshake failed")) {
			/* XXX This is an error we can handle,  we should ask user if they
			 * want to trust the server anyway, ie. add a "security exception"
			 * for this particular server.
			 *
			 * However I won't do it now, because:
			 * - libsoup 3.x will break whatever solution I can come up with
			 *   - <https://gitlab.gnome.org/GNOME/libsoup/commit/b4b865d>
			 *   - <https://gitlab.gnome.org/GNOME/libsoup/commit/70c1b90>
			 * - more code refactoring is needed on Goodvibes side (the interaction
			 *   between GvPlayer, GvStation and GvPlaylist is awkward at best)
			 * - nobody complained on the bug tracker anyway
			 *
			 * So I'll try to get back at it when libsoup 3.x is out.
			 */
			INFO("XXX Handler not implemented yet");
		}
		goto end;
	} else {
		SoupMessageHeaders *headers = msg->response_headers;
		const gchar *content_type = NULL;

		if (headers)
			content_type = soup_message_headers_get_content_type(headers, NULL);

		DEBUG("Playlist downloaded (Content-Type: %s)", content_type);
	}

	if (msg->response_body->length == 0) {
		WARNING("Empty playlist");
		goto end;
	}

	//PRINT("%s", msg->response_body->data);

	/* Get the right parser */
	switch (priv->format) {
	case GV_PLAYLIST_FORMAT_M3U:
		parser = parse_playlist_m3u;
		break;
	case GV_PLAYLIST_FORMAT_PLS:
		parser = parse_playlist_pls;
		break;
	case GV_PLAYLIST_FORMAT_ASX:
		parser = parse_playlist_asx;
		break;
	case GV_PLAYLIST_FORMAT_XSPF:
		parser = parse_playlist_xspf;
		break;
	default:
		WARNING("No parser for playlist format: %d", priv->format);
		goto end;
	}

	/* Parse */
	if (priv->streams)
		g_slist_free_full(priv->streams, g_free);

	priv->streams = parser(msg->response_body->data, msg->response_body->length);

	/* Was it parsed successfully ? */
	if (priv->streams == NULL) {
		WARNING("Failed to parse playlist");
		goto end;
	}

	DEBUG("%d streams found:", g_slist_length(priv->streams));

	for (item = priv->streams; item; item = item->next) {
		DEBUG(". %s", item->data);
	}


end:
	// TODO Is it ok to unref that here ?
	g_object_unref(session);

	/* msg needs not to be unreferenced. According to the doc,
	 * it's consumed when using the queue() API.
	 */

	/* Emit completion signal */
	g_signal_emit(self, signals[SIGNAL_DOWNLOADED], 0);
}

/*
 * Property accessors
 */

const gchar *
gv_playlist_get_uri(GvPlaylist *self)
{
	return self->priv->uri;
}

static void
gv_playlist_set_uri(GvPlaylist *self, const gchar *uri)
{
	GvPlaylistPrivate *priv = self->priv;

	/* This is a construct-only property */
	g_assert_null(priv->uri);
	g_assert_nonnull(uri);
	priv->uri = g_strdup(uri);

	/* Set format */
	priv->format = gv_playlist_get_format(uri);
}

GSList *
gv_playlist_get_stream_list(GvPlaylist *self)
{
	return self->priv->streams;
}

static void
gv_playlist_get_property(GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	GvPlaylist *self = GV_PLAYLIST(object);

	TRACE_GET_PROPERTY(object, property_id, value, pspec);

	switch (property_id) {
	case PROP_URI:
		g_value_set_string(value, gv_playlist_get_uri(self));
		break;
	case PROP_STREAM_LIST:
		g_value_set_pointer(value, gv_playlist_get_stream_list(self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void
gv_playlist_set_property(GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	GvPlaylist *self = GV_PLAYLIST(object);

	TRACE_SET_PROPERTY(object, property_id, value, pspec);

	switch (property_id) {
	case PROP_URI:
		gv_playlist_set_uri(self, g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

/*
 * Public methods
 */

void
gv_playlist_download(GvPlaylist *self, gboolean insecure, const gchar *user_agent)
{
	GvPlaylistPrivate *priv = self->priv;
	SoupSession *session;
	SoupMessage *msg;

	DEBUG("Downloading playlist '%s' (user-agent: '%s')", priv->uri, user_agent);
	session = soup_session_new_with_options(SOUP_SESSION_SSL_STRICT, !insecure,
	                                        SOUP_SESSION_USER_AGENT, user_agent,
	                                        NULL);
	msg = soup_message_new("GET", priv->uri);

	soup_session_queue_message(session, msg,
	                           (SoupSessionCallback) on_message_completed,
	                           self);
}

GvPlaylist *
gv_playlist_new(const gchar *uri)
{
	return g_object_new(GV_TYPE_PLAYLIST, "uri", uri, NULL);
}

/*
 * GObject methods
 */

static void
gv_playlist_finalize(GObject *object)
{
	GvPlaylistPrivate *priv = GV_PLAYLIST(object)->priv;

	TRACE("%p", object);

	/* Free any allocated resources */
	g_free(priv->uri);

	/* Chain up */
	G_OBJECT_CHAINUP_FINALIZE(gv_playlist, object);
}

static void
gv_playlist_constructed(GObject *object)
{
	GvPlaylist *self = GV_PLAYLIST(object);
	GvPlaylistPrivate *priv = self->priv;

	TRACE("%p", object);

	/* Ensure construct-only properties have been set */
	g_assert_nonnull(priv->uri);

	/* Chain up */
	G_OBJECT_CHAINUP_CONSTRUCTED(gv_playlist, object);
}

static void
gv_playlist_init(GvPlaylist *self)
{
	TRACE("%p", self);

	/* Initialize private pointer */
	self->priv = gv_playlist_get_instance_private(self);
}

static void
gv_playlist_class_init(GvPlaylistClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	TRACE("%p", class);

	/* Override GObject methods */
	object_class->finalize = gv_playlist_finalize;
	object_class->constructed = gv_playlist_constructed;

	/* Properties */
	object_class->get_property = gv_playlist_get_property;
	object_class->set_property = gv_playlist_set_property;

	properties[PROP_URI] =
	        g_param_spec_string("uri", "Uri", NULL, NULL,
	                            GV_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

	properties[PROP_STREAM_LIST] =
	        g_param_spec_pointer("stream-list", "Stream list", NULL,
	                             GV_PARAM_READABLE);

	g_object_class_install_properties(object_class, PROP_N, properties);

	/* Signals */
	signals[SIGNAL_DOWNLOADED] =
	        g_signal_new("downloaded", G_TYPE_FROM_CLASS(class),
	                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			     G_TYPE_NONE, 0);
}

/*
 * Class methods
 */

GvPlaylistFormat
gv_playlist_get_format(const gchar *uri_string)
{
	GvPlaylistFormat fmt = GV_PLAYLIST_FORMAT_UNKNOWN;
	SoupURI *uri;
	const gchar *path;
	const gchar *ext;

	/* Parse the uri */
	uri = soup_uri_new(uri_string);
	if (uri == NULL) {
		INFO("Invalid uri '%s'", uri_string);
		return GV_PLAYLIST_FORMAT_UNKNOWN;
	}

	/* Get the path */
	path = soup_uri_get_path(uri);

	/* Get the extension of the path */
	ext = strrchr(path, '.');
	if (ext)
		ext++;
	else
		ext = "\0";

	/* Match with supported extensions */
	if (!g_ascii_strcasecmp(ext, "m3u"))
		fmt = GV_PLAYLIST_FORMAT_M3U;
	else if (!g_ascii_strcasecmp(ext, "ram"))
		fmt = GV_PLAYLIST_FORMAT_M3U;
	else if (!g_ascii_strcasecmp(ext, "pls"))
		fmt = GV_PLAYLIST_FORMAT_PLS;
	else if (!g_ascii_strcasecmp(ext, "asx"))
		fmt = GV_PLAYLIST_FORMAT_ASX;
	else if (!g_ascii_strcasecmp(ext, "xspf"))
		fmt = GV_PLAYLIST_FORMAT_XSPF;

	/* Cleanup */
	soup_uri_free(uri);

	return fmt;
}
