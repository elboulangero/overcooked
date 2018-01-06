/*
 * Goodvibes Radio Player
 *
 * Copyright (C) 2015-2017 Arnaud Rebillout
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <glib.h>
#include <glib-object.h>

#include "additions/glib-object.h"
#include "framework/gv-framework.h"

#include "core/gv-station-list.h"

// WISHED Try with a huge number of stations to see how it behaves.
//        It might be slow. The implementation never tried to be fast.

/*
 * FIP <http://www.fipradio.fr/>
 * Just the best radios you'll ever listen to.
 */

#define FIP_STATIONS      \
        "<Station>" \
        "  <name>FIP Paris</name>" \
        "  <uri>http://direct.fipradio.fr/live/fip-midfi.mp3</uri>" \
        "</Station>" \
        "<Station>" \
        "  <name>FIP Autour du Rock</name>" \
        "  <uri>http://direct.fipradio.fr/live/fip-webradio1.mp3</uri>" \
        "</Station>" \
        "<Station>" \
        "  <name>FIP Autour du Jazz</name>" \
        "  <uri>http://direct.fipradio.fr/live/fip-webradio2.mp3</uri>" \
        "</Station>" \
        "<Station>" \
        "  <name>FIP Autour du Groove</name>" \
        "  <uri>http://direct.fipradio.fr/live/fip-webradio3.mp3</uri>" \
        "</Station>" \
        "<Station>" \
        "  <name>FIP Autour du Monde</name>" \
        "  <uri>http://direct.fipradio.fr/live/fip-webradio4.mp3</uri>" \
        "</Station>" \
        "<Station>" \
        "  <name>FIP Tout nouveau, tout FIP</name>" \
        "  <uri>http://direct.fipradio.fr/live/fip-webradio5.mp3</uri>" \
        "</Station>" \
        "<Station>" \
        "  <name>FIP Evenementielle</name>" \
        "  <uri>http://direct.fipradio.fr/live/fip-webradio6.mp3</uri>" \
        "</Station>"

/*
 * More of my favorite french radios.
 * - Nova       <http://www.novaplanet.com/>
 * - Grenouille <http://www.radiogrenouille.com/>
 */

#define FRENCH_STATIONS   \
        "<Station>" \
        "  <name>Nova</name>" \
        "  <uri>http://broadcast.infomaniak.net/radionova-high.mp3</uri>" \
        "</Station>" \
        "<Station>" \
        "  <name>Radio Grenouille</name>" \
        "  <uri>http://live.radiogrenouille.com/live</uri>" \
        "</Station>"

/*
 * Broken stations.
 */

#define TESTING_BROKEN_STATIONS   \
        "<Station>" \
        "  <name>Broken - FIP old url</name>" \
        "  <uri>http://audio.scdn.arkena.com/11016/fip-midfi128.mp3</uri>" \
        "</Station>"

/*
 * Various playlist formats.
 * - Swiss Internet Radio (Public Domain Radio) <http://www.swissradio.ch/>
 */

#define TESTING_PLAYLIST_STATIONS         \
        "<Station>" \
        "  <name>M3U - Swiss Internet Radio Classical</name>" \
        "  <uri>http://www.swissradio.ch/streams/6034.m3u</uri>" \
        "</Station>" \
        "<Station>" \
        "  <name>ASX - Swiss Internet Radio Classical</name>" \
        "  <uri>http://www.swissradio.ch/streams/6034.asx</uri>" \
        "</Station>" \
        "<Station>" \
        "  <name>RAM - Swiss Internet Radio Classical</name>" \
        "  <uri>http://www.swissradio.ch/streams/6034.ram</uri>" \
        "</Station>"

/*
 * More radios, for testing.
 */

#define TESTING_MORE_STATIONS     \
        "<Station>" \
        "  <uri>http://www.netradio.fr:8000/A0RemzouilleRadio.xspf</uri>" \
        "</Station>" \
        "<Station>" \
        "  <uri>http://player.100p.nl/livestream.asx</uri>" \
        "</Station>" \
        "<Station>" \
        "  <uri>http://vt-net.org/WebRadio/live/8056.m3u</uri>" \
        "</Station>" \
        "<Station>" \
        "  <uri>'http://www.neradio.se/listen.pls</uri>" \
        "</Station>"

/*
 * Default station list, loaded if no station list file is found
 */

#define DEFAULT_STATIONS_DEV      \
        FIP_STATIONS \
        FRENCH_STATIONS \
        TESTING_BROKEN_STATIONS \
        TESTING_PLAYLIST_STATIONS \
        TESTING_MORE_STATIONS \

#define DEFAULT_STATIONS_PROD     \
        FIP_STATIONS \
        FRENCH_STATIONS

#define DEFAULT_STATION_LIST      \
        "<Stations>" \
        DEFAULT_STATIONS_PROD \
        "</Stations>"

/*
 * Save delay - how long do we wait to write configuration to file
 */

#define SAVE_DELAY 1

/*
 * Signals
 */

enum {
	SIGNAL_LOADED,
	SIGNAL_STATION_ADDED,
	SIGNAL_STATION_REMOVED,
	SIGNAL_STATION_MODIFIED,
	SIGNAL_STATION_MOVED,
	/* Number of signals */
	SIGNAL_N
};

static guint signals[SIGNAL_N];

/*
 * GObject definitions
 */

struct _GvStationListPrivate {
	/* Load/save pathes */
	GSList *load_pathes;
	gchar  *save_path;
	/* Timeout id, > 0 if a save operation is scheduled */
	guint   save_source_id;
	/* Ordered list of stations */
	GList  *stations;
	/* Shuffled list of stations, automatically created
	 * and destroyed when needed.
	 */
	GList  *shuffled;
};

typedef struct _GvStationListPrivate GvStationListPrivate;

struct _GvStationList {
	/* Parent instance structure */
	GObject               parent_instance;
	/* Private data */
	GvStationListPrivate *priv;
};

G_DEFINE_TYPE_WITH_CODE(GvStationList, gv_station_list, G_TYPE_OBJECT,
                        G_ADD_PRIVATE(GvStationList)
                        G_IMPLEMENT_INTERFACE(GV_TYPE_ERRORABLE, NULL))

/*
 * Markup handling
 */

struct _GvMarkupParsing {
	/* Persistent during the whole parsing process */
	GList  *list;
	/* Current iteration */
	gchar **cur;
	gchar  *name;
	gchar  *uri;
	gchar  *user_agent;
};

typedef struct _GvMarkupParsing GvMarkupParsing;

static void
markup_on_text(GMarkupParseContext  *context G_GNUC_UNUSED,
               const gchar          *text,
               gsize                 text_len G_GNUC_UNUSED,
               gpointer              user_data,
               GError              **error G_GNUC_UNUSED)
{
	GvMarkupParsing *parsing = user_data;

	/* Happens all the time */
	if (parsing->cur == NULL)
		return;

	/* Should never happen */
	g_assert_null(*parsing->cur);

	/* Save text */
	*parsing->cur = g_strdup(text);

	/* Cleanup */
	parsing->cur = NULL;
}

static void
markup_on_end_element(GMarkupParseContext  *context G_GNUC_UNUSED,
                      const gchar          *element_name G_GNUC_UNUSED,
                      gpointer              user_data,
                      GError              **error G_GNUC_UNUSED)
{
	GvMarkupParsing *parsing = user_data;
	GvStation *station;

	/* We only care when we leave a station node */
	if (g_strcmp0(element_name, "Station"))
		return;

	/* Discard stations with no uri */
	if (parsing->uri == NULL || parsing->uri[0] == '\0') {
		DEBUG("Encountered station without uri (named '%s')", parsing->name);
		goto cleanup;
	}

	/* Create a new station */
	station = gv_station_new(parsing->name, parsing->uri);
	if (parsing->user_agent)
		gv_station_set_user_agent(station, parsing->user_agent);

	/* We must take ownership right now */
	g_object_ref_sink(station);

	/* Add to list, use prepend for efficiency */
	parsing->list = g_list_prepend(parsing->list, station);

cleanup:
	/* Cleanup */
	g_free(parsing->name);
	g_free(parsing->uri);
	g_free(parsing->user_agent);
	parsing->name = NULL;
	parsing->uri = NULL;
	parsing->user_agent = NULL;
}

static void
markup_on_start_element(GMarkupParseContext  *context G_GNUC_UNUSED,
                        const gchar          *element_name,
                        const gchar         **attribute_names G_GNUC_UNUSED,
                        const gchar         **attribute_values G_GNUC_UNUSED,
                        gpointer              user_data,
                        GError              **error G_GNUC_UNUSED)
{
	GvMarkupParsing *parsing = user_data;

	/* Expected top node */
	if (!g_strcmp0(element_name, "Stations"))
		return;

	/* Entering a station node */
	if (!g_strcmp0(element_name, "Station")) {
		g_assert_null(parsing->name);
		g_assert_null(parsing->uri);
		g_assert_null(parsing->user_agent);
		return;
	}

	/* Name property */
	if (!g_strcmp0(element_name, "name")) {
		g_assert_null(parsing->cur);
		parsing->cur = &parsing->name;
		return;
	}

	/* Uri property */
	if (!g_strcmp0(element_name, "uri")) {
		g_assert_null(parsing->cur);
		parsing->cur = &parsing->uri;
		return;
	}

	/* User-agent property */
	if (!g_strcmp0(element_name, "user-agent")) {
		g_assert_null(parsing->user_agent);
		parsing->cur = &parsing->user_agent;
		return;
	}

	WARNING("Unexpected element: '%s'", element_name);
}

static void
markup_on_error(GMarkupParseContext *context G_GNUC_UNUSED,
                GError              *error   G_GNUC_UNUSED,
                gpointer             user_data)
{
	GvMarkupParsing *parsing = user_data;

	parsing->cur = NULL;
	g_free(parsing->name);
	parsing->name = NULL;
	g_free(parsing->uri);
	parsing->uri = NULL;
	g_free(parsing->user_agent);
	parsing->user_agent = NULL;
}

static GList *
parse_markup(const gchar *text, GError **err)
{
	GMarkupParseContext *context;
	GMarkupParser parser = {
		markup_on_start_element,
		markup_on_end_element,
		markup_on_text,
		NULL,
		markup_on_error,
	};
	GvMarkupParsing parsing = {
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	};

	context = g_markup_parse_context_new(&parser, 0, &parsing, NULL);
	g_markup_parse_context_parse(context, text, -1, err);
	g_markup_parse_context_free(context);

	return g_list_reverse(parsing.list);
}

static GString *
g_string_append_markup_tag_escaped(GString *string, const gchar *tag, const gchar *value)
{
	gchar *escaped;

	escaped = g_markup_printf_escaped("    <%s>%s</%s>\n", tag, value, tag);
	g_string_append(string, escaped);
	g_free(escaped);

	return string;
}

static gchar *
print_markup_station(GvStation *station)
{
	const gchar *name = gv_station_get_name(station);
	const gchar *uri = gv_station_get_uri(station);
	const gchar *user_agent = gv_station_get_user_agent(station);
	GString *string;

	/* A station is supposed to have an uri */
	if (uri == NULL) {
		WARNING("Station (%s) has no uri !", name);
		return NULL;
	}

	/* Write station in markup fashion */
	string = g_string_new("  <Station>\n");

	if (uri)
		g_string_append_markup_tag_escaped(string, "uri", uri);

	if (name)
		g_string_append_markup_tag_escaped(string, "name", name);

	if (user_agent)
		g_string_append_markup_tag_escaped(string, "user-agent", user_agent);

	g_string_append(string, "  </Station>\n");

	/* Return */
	return g_string_free(string, FALSE);
}

static gchar *
print_markup(GList *list, GError **err G_GNUC_UNUSED)
{
	GList *item;
	GString *string = g_string_new(NULL);

	g_string_append(string, "<Stations>\n");

	for (item = list; item; item = item->next) {
		GvStation *station = GV_STATION(item->data);
		gchar *text;

		text = print_markup_station(station);
		if (text == NULL)
			continue;

		g_string_append(string, text);
		g_free(text);
	}

	g_string_append(string, "</Stations>");

	return g_string_free(string, FALSE);
}

/*
 * Iterator implementation
 */

struct _GvStationListIter {
	GList *head, *item;
};

GvStationListIter *
gv_station_list_iter_new(GvStationList *self)
{
	GList *list = self->priv->stations;
	GvStationListIter *iter;

	iter = g_new0(GvStationListIter, 1);
	iter->head = g_list_copy_deep(list, (GCopyFunc) g_object_ref, NULL);
	iter->item = iter->head;

	return iter;
}

void
gv_station_list_iter_free(GvStationListIter *iter)
{
	g_return_if_fail(iter != NULL);

	g_list_free_full(iter->head, g_object_unref);
	g_free(iter);
}

gboolean
gv_station_list_iter_loop(GvStationListIter *iter, GvStation **station)
{
	g_return_val_if_fail(iter != NULL, FALSE);
	g_return_val_if_fail(station != NULL, FALSE);

	*station = NULL;

	if (iter->item == NULL)
		return FALSE;

	*station = iter->item->data;
	iter->item = iter->item->next;

	return TRUE;
}

/*
 * GList additions
 */

static gint
glist_sortfunc_random(gconstpointer a G_GNUC_UNUSED, gconstpointer b G_GNUC_UNUSED)
{
	return g_random_boolean() ? 1 : -1;
}

static GList *
g_list_shuffle(GList *list)
{
	/* Shuffle twice. Credits goes to:
	 * http://www.linuxforums.org/forum/programming-scripting/
	 * 202125-fastest-way-shuffle-linked-list.html
	 */
	list = g_list_sort(list, glist_sortfunc_random);
	list = g_list_sort(list, glist_sortfunc_random);
	return list;
}

static GList *
g_list_copy_deep_shuffle(GList *list, GCopyFunc func, gpointer user_data)
{
	list = g_list_copy_deep(list, func, user_data);
	list = g_list_shuffle(list);
	return list;
}

/*
 * Helpers
 */

static gint
are_stations_similar(GvStation *s1, GvStation *s2)
{
	if (s1 == s2) {
		WARNING("Stations %p and %p are the same", s1, s2);
		return 0;
	}

	if (s1 == NULL || s2 == NULL)
		return -1;

	/* Compare uids */
	const gchar *s1_uid, *s2_uid;
	s1_uid = gv_station_get_uid(s1);
	s2_uid = gv_station_get_uid(s2);

	if (!g_strcmp0(s1_uid, s2_uid)) {
		WARNING("Stations %p and %p have the same uid '%s'", s1, s2, s1_uid);
		return 0;
	}

	/* Compare names.
	 * Two stations who don't have name are different.
	 */
	const gchar *s1_name, *s2_name;
	s1_name = gv_station_get_name(s1);
	s2_name = gv_station_get_name(s2);

	if (s1_name == NULL && s2_name == NULL)
		return -1;

	if (!g_strcmp0(s1_name, s2_name)) {
		DEBUG("Stations %p and %p have the same name '%s'", s1, s2, s1_name);
		return 0;
	}

	/* Compare uris */
	const gchar *s1_uri, *s2_uri;
	s1_uri = gv_station_get_uri(s1);
	s2_uri = gv_station_get_uri(s2);

	if (!g_strcmp0(s1_uri, s2_uri)) {
		DEBUG("Stations %p and %p have the same uri '%s'", s1, s2, s1_uri);
		return 0;
	}

	return -1;
}

/*
 * Signal handlers
 */

static gboolean
when_timeout_save_station_list(gpointer data)
{
	GvStationList *self = GV_STATION_LIST(data);
	GvStationListPrivate *priv = self->priv;

	gv_station_list_save(self);

	priv->save_source_id = 0;

	return G_SOURCE_REMOVE;
}

static void
gv_station_list_save_delayed(GvStationList *self)
{
	GvStationListPrivate *priv = self->priv;

	if (priv->save_source_id > 0)
		g_source_remove(priv->save_source_id);

	priv->save_source_id =
	        g_timeout_add_seconds(SAVE_DELAY, when_timeout_save_station_list, self);
}

static void
on_station_notify(GvStation     *station,
                  GParamSpec     *pspec,
                  GvStationList *self)
{
	const gchar *property_name = g_param_spec_get_name(pspec);

	TRACE("%s, %s, %p", gv_station_get_uid(station), property_name, self);

	/* We might want to save changes */
	if (!g_strcmp0(property_name, "uri") ||
	    !g_strcmp0(property_name, "name")) {
		gv_station_list_save_delayed(self);
	}

	/* Emit signal */
	g_signal_emit(self, signals[SIGNAL_STATION_MODIFIED], 0, station);
}

/*
 * Public functions
 */

void
gv_station_list_remove(GvStationList *self, GvStation *station)
{
	GvStationListPrivate *priv = self->priv;
	GList *item;

	/* Ensure a valid station was given */
	if (station == NULL) {
		WARNING("Attempting to remove NULL station");
		return;
	}

	/* Give info */
	INFO("Removing station '%s'", gv_station_get_name_or_uri(station));

	/* Check that we own this station at first. If we don't find it
	 * in our internal list, it's probably a programming error.
	 */
	item = g_list_find(priv->stations, station);
	if (item == NULL) {
		WARNING("GvStation %p (%s) not found in list",
		        station, gv_station_get_uid(station));
		return;
	}

	/* Disconnect signal handlers */
	g_signal_handlers_disconnect_by_data(station, self);

	/* Remove from list */
	priv->stations = g_list_remove_link(priv->stations, item);
	g_list_free(item);

	/* Unown the station */
	g_object_unref(station);

	/* Rebuild the shuffled station list */
	if (priv->shuffled) {
		g_list_free_full(priv->shuffled, g_object_unref);
		priv->shuffled = g_list_copy_deep_shuffle(priv->stations,
		                 (GCopyFunc) g_object_ref, NULL);
	}

	/* Emit a signal */
	g_signal_emit(self, signals[SIGNAL_STATION_REMOVED], 0, station);

	/* Save */
	gv_station_list_save_delayed(self);
}

void
gv_station_list_insert(GvStationList *self, GvStation *station, gint pos)
{
	GvStationListPrivate *priv = self->priv;
	GList *similar_item;

	/* Ensure a valid station was given */
	if (station == NULL) {
		WARNING("Attempting to insert NULL station");
		return;
	}

	/* Give info */
	INFO("Inserting station '%s'", gv_station_get_name_or_uri(station));

	/* Check that the station is not already part of the list.
	 * Duplicates are a programming error, we must warn about that.
	 * Identical fields are an user error.
	 * Warnings and such are encapsulated in the GCompareFunc used
	 * here, this is messy but temporary (hopefully).
	 */
	similar_item = g_list_find_custom(priv->stations, station,
	                                  (GCompareFunc) are_stations_similar);
	if (similar_item)
		return;

	/* Take ownership of the station */
	g_object_ref_sink(station);

	/* Add to the list at the right position */
	priv->stations = g_list_insert(priv->stations, station, pos);

	/* Connect to notify signal */
	g_signal_connect_object(station, "notify", G_CALLBACK(on_station_notify), self, 0);

	/* Rebuild the shuffled station list */
	if (priv->shuffled) {
		g_list_free_full(priv->shuffled, g_object_unref);
		priv->shuffled = g_list_copy_deep_shuffle(priv->stations,
		                 (GCopyFunc) g_object_ref, NULL);
	}

	/* Emit a signal */
	g_signal_emit(self, signals[SIGNAL_STATION_ADDED], 0, station);

	/* Save */
	gv_station_list_save_delayed(self);
}

/* Insert a station before another.
 * If 'before' is NULL or is not found, the station is appended at the end of the list.
 */
void
gv_station_list_insert_before(GvStationList *self, GvStation *station, GvStation *before)
{
	GvStationListPrivate *priv = self->priv;
	gint pos = -1;

	pos = g_list_index(priv->stations, before);
	gv_station_list_insert(self, station, pos);
}

/* Insert a station after another.
 * If 'after' is NULL or not found, the station is appended at the beginning of the list.
 */
void
gv_station_list_insert_after(GvStationList *self, GvStation *station, GvStation *after)
{
	GvStationListPrivate *priv = self->priv;
	gint pos = 0;

	pos = g_list_index(priv->stations, after);
	pos += 1; /* tricky but does what we want even for pos == -1 */
	gv_station_list_insert(self, station, pos);
}

void
gv_station_list_prepend(GvStationList *self, GvStation *station)
{
	gv_station_list_insert_after(self, station, NULL);
}

void
gv_station_list_append(GvStationList *self, GvStation *station)
{
	gv_station_list_insert_before(self, station, NULL);
}

void
gv_station_list_move(GvStationList *self, GvStation *station, gint pos)
{
	GvStationListPrivate *priv = self->priv;
	GList *item;

	/* Find the station */
	item = g_list_find(priv->stations, station);
	if (item == NULL) {
		WARNING("GvStation %p (%s) not found in list",
		        station, gv_station_get_uid(station));
		return;
	}

	/* Move it */
	priv->stations = g_list_remove_link(priv->stations, item);
	g_list_free(item);
	priv->stations = g_list_insert(priv->stations, station, pos);

	/* Emit a signal */
	g_signal_emit(self, signals[SIGNAL_STATION_MOVED], 0, station);

	/* Save */
	gv_station_list_save_delayed(self);
}

/* Move a station before another.
 * If 'before' is NULL or not found, the station is inserted at the end of the list.
 */
void
gv_station_list_move_before(GvStationList *self, GvStation *station, GvStation *before)
{
	GvStationListPrivate *priv = self->priv;
	gint pos = -1;

	pos = g_list_index(priv->stations, before);
	gv_station_list_move(self, station, pos);
}

void
gv_station_list_move_after(GvStationList *self, GvStation *station, GvStation *after)
{
	GvStationListPrivate *priv = self->priv;
	gint pos = 0;

	pos = g_list_index(priv->stations, after);
	pos += 1; /* tricky but does what we want even for pos == -1 */
	gv_station_list_move(self, station, pos);
}

void
gv_station_list_move_first(GvStationList *self, GvStation *station)
{
	gv_station_list_move_after(self, station, NULL);
}

void
gv_station_list_move_last(GvStationList *self, GvStation *station)
{
	gv_station_list_move_before(self, station, NULL);
}

GvStation *
gv_station_list_prev(GvStationList *self, GvStation *station,
                     gboolean repeat, gboolean shuffle)
{
	GvStationListPrivate *priv = self->priv;
	GList *stations, *item;

	/* Pickup the right station list, create shuffle list if needed */
	if (shuffle) {
		if (priv->shuffled == NULL) {
			priv->shuffled = g_list_copy_deep_shuffle(priv->stations,
			                 (GCopyFunc) g_object_ref, NULL);
		}
		stations = priv->shuffled;
	} else {
		if (priv->shuffled) {
			g_list_free_full(priv->shuffled, g_object_unref);
			priv->shuffled = NULL;
		}
		stations = priv->stations;
	}

	/* If the station list is empty, bail out */
	if (stations == NULL)
		return NULL;

	/* Return last station for NULL argument */
	if (station == NULL)
		return g_list_last(stations)->data;

	/* Try to find station in station list */
	item = g_list_find(stations, station);
	if (item == NULL)
		return NULL;

	/* Return previous station if any */
	item = item->prev;
	if (item)
		return item->data;

	/* Without repeat, there's no more station */
	if (!repeat)
		return NULL;

	/* With repeat, we may re-shuffle, then return the last station */
	if (shuffle) {
		GList *last_item;

		stations = g_list_shuffle(priv->shuffled);

		/* In case the last station (that we're about to return) happens to be
		 * the same as the current station, we do a little a magic trick.
		 */
		last_item = g_list_last(stations);
		if (last_item->data == station) {
			stations = g_list_remove_link(stations, last_item);
			stations = g_list_prepend(stations, last_item->data);
			g_list_free(last_item);
		}

		priv->shuffled = stations;
	}

	return g_list_last(stations)->data;
}

GvStation *
gv_station_list_next(GvStationList *self, GvStation *station,
                     gboolean repeat, gboolean shuffle)
{
	GvStationListPrivate *priv = self->priv;
	GList *stations, *item;

	/* Pickup the right station list, create shuffle list if needed */
	if (shuffle) {
		if (priv->shuffled == NULL) {
			priv->shuffled = g_list_copy_deep_shuffle(priv->stations,
			                 (GCopyFunc) g_object_ref, NULL);
		}
		stations = priv->shuffled;
	} else {
		if (priv->shuffled) {
			g_list_free_full(priv->shuffled, g_object_unref);
			priv->shuffled = NULL;
		}
		stations = priv->stations;
	}

	/* If the station list is empty, bail out */
	if (stations == NULL)
		return NULL;

	/* Return first station for NULL argument */
	if (station == NULL)
		return stations->data;

	/* Try to find station in station list */
	item = g_list_find(stations, station);
	if (item == NULL)
		return NULL;

	/* Return next station if any */
	item = item->next;
	if (item)
		return item->data;

	/* Without repeat, there's no more station */
	if (!repeat)
		return NULL;

	/* With repeat, we may re-shuffle, then return the first station */
	if (shuffle) {
		GList *first_item;

		stations = g_list_shuffle(priv->shuffled);

		/* In case the first station (that we're about to return) happens to be
		 * the same as the current station, we do a little a magic trick.
		 */
		first_item = g_list_first(stations);
		if (first_item->data == station) {
			stations = g_list_remove_link(stations, first_item);
			stations = g_list_append(stations, first_item->data);
			g_list_free(first_item);
		}

		priv->shuffled = stations;
	}

	return stations->data;
}

GvStation *
gv_station_list_first(GvStationList *self)
{
	GList *stations = self->priv->stations;

	if (stations == NULL)
		return NULL;

	return g_list_first(stations)->data;
}

GvStation *
gv_station_list_last(GvStationList *self)
{
	GList *stations = self->priv->stations;

	if (stations == NULL)
		return NULL;

	return g_list_last(stations)->data;
}

GvStation *
gv_station_list_find(GvStationList *self, GvStation *station)
{
	GList *stations = self->priv->stations;
	GList *item;

	item = g_list_find(stations, station);

	return item ? item->data : NULL;
}

GvStation *
gv_station_list_find_by_name(GvStationList *self, const gchar *name)
{
	GList *item;

	/* Ensure station name is valid */
	if (name == NULL) {
		WARNING("Attempting to find a station with NULL name");
		return NULL;
	}

	/* Forbid empty names */
	if (!g_strcmp0(name, ""))
		return NULL;

	/* Iterate on station list */
	for (item = self->priv->stations; item; item = item->next) {
		GvStation *station = item->data;
		if (!g_strcmp0(name, gv_station_get_name(station)))
			return station;
	}

	return NULL;
}

GvStation *
gv_station_list_find_by_uri(GvStationList *self, const gchar *uri)
{
	GList *item;

	/* Ensure station name is valid */
	if (uri == NULL) {
		WARNING("Attempting to find a station with NULL uri");
		return NULL;
	}

	/* Iterate on station list */
	for (item = self->priv->stations; item; item = item->next) {
		GvStation *station = item->data;
		if (!g_strcmp0(uri, gv_station_get_uri(station)))
			return station;
	}

	return NULL;
}

GvStation *
gv_station_list_find_by_uid(GvStationList *self, const gchar *uid)
{
	GList *item;

	/* Ensure station name is valid */
	if (uid == NULL) {
		WARNING("Attempting to find a station with NULL uid");
		return NULL;
	}

	/* Iterate on station list */
	for (item = self->priv->stations; item; item = item->next) {
		GvStation *station = item->data;
		if (!g_strcmp0(uid, gv_station_get_uid(station)))
			return station;
	}

	return NULL;
}

GvStation  *
gv_station_list_find_by_guessing(GvStationList *self, const gchar *string)
{
	if (is_uri_scheme_supported(string))
		return gv_station_list_find_by_uri(self, string);
	else
		/* Assume it's the station name */
		return gv_station_list_find_by_name(self, string);
}

void
gv_station_list_save(GvStationList *self)
{
	GvStationListPrivate *priv = self->priv;
	GError *err = NULL;
	gchar *text;

	/* Stringify data */
	text = print_markup(priv->stations, &err);
	if (err)
		goto cleanup;

	/* Write to file */
	gv_file_write_sync(priv->save_path, text, &err);

cleanup:
	/* Cleanup */
	g_free(text);

	/* Handle error */
	if (err == NULL) {
		INFO("Station list saved to '%s'", priv->save_path);
	} else {
		WARNING("Failed to save station list: %s", err->message);
		gv_errorable_emit_error(GV_ERRORABLE(self), _("%s: %s"),
		                        _("Failed to save station list"), err->message);

		g_error_free(err);
	}
}

void
gv_station_list_load(GvStationList *self)
{
	GvStationListPrivate *priv = self->priv;
	GSList *item = NULL;
	GList *sta_item;

	TRACE("%p", self);

	/* This should be called only once at startup */
	g_assert_null(priv->stations);

	/* Load from a list of pathes */
	for (item = priv->load_pathes; item; item = item->next) {
		GError *err = NULL;
		const gchar *path = item->data;
		gchar *text;

		/* Attempt to read file */
		gv_file_read_sync(path, &text, &err);
		if (err) {
			WARNING("%s", err->message);
			g_clear_error(&err);
			continue;
		}

		/* Attempt to parse it */
		priv->stations = parse_markup(text, &err);
		g_free(text);
		if (err) {
			WARNING("Failed to parse '%s': %s", path, err->message);
			g_clear_error(&err);
			continue;
		}

		/* Success */
		break;
	}

	/* Check if we got something */
	if (item) {
		const gchar *loaded_path = item->data;

		INFO("Station list loaded from file '%s'", loaded_path);
	} else {
		GError *err = NULL;

		INFO("No valid station list file found, using hard-coded default");

		priv->stations = parse_markup(DEFAULT_STATION_LIST, &err);
		if (err) {
			ERROR("%s", err->message);
			/* Program execution stops here */
		}
	}

	/* Dump the number of stations */
	DEBUG("Station list has %u stations", gv_station_list_length(self));

	/* Register a notify handler for each station */
	for (sta_item = priv->stations; sta_item; sta_item = sta_item->next) {
		GvStation *station = sta_item->data;

		g_signal_connect_object(station, "notify", G_CALLBACK(on_station_notify), self, 0);
	}

	/* Emit a signal to indicate that the list has been loaded */
	g_signal_emit(self, signals[SIGNAL_LOADED], 0);
}

guint
gv_station_list_length(GvStationList *self)
{
	GvStationListPrivate *priv = self->priv;

	return g_list_length(priv->stations);
}

GvStationList *
gv_station_list_new(void)
{
	return g_object_new(GV_TYPE_STATION_LIST, NULL);
}

/*
 * GObject methods
 */

static void
gv_station_list_finalize(GObject *object)
{
	GvStationList *self = GV_STATION_LIST(object);
	GvStationListPrivate *priv = self->priv;
	GList *item;

	TRACE("%p", object);

	/* Run any pending save operation */
	if (priv->save_source_id > 0)
		when_timeout_save_station_list(self);

	/* Free shuffled station list */
	g_list_free_full(priv->shuffled, g_object_unref);

	/* Free station list and ensure no memory is leaked. This works only if the
	 * station list is the last object to hold references to stations. In other
	 * words, the station list must be the last object finalized.
	 */
	for (item = priv->stations; item; item = item->next) {
		g_object_add_weak_pointer(G_OBJECT(item->data), &(item->data));
		g_object_unref(item->data);
		if (item->data != NULL)
			WARNING("Station '%s' has not been finalized !",
			        gv_station_get_name_or_uri(GV_STATION(item->data)));
	}
	g_list_free(priv->stations);

	/* Free pathes */
	g_free(priv->save_path);
	g_slist_free_full(priv->load_pathes, g_free);

	/* Chain up */
	G_OBJECT_CHAINUP_FINALIZE(gv_station_list, object);
}

static void
gv_station_list_constructed(GObject *object)
{
	GvStationList *self = GV_STATION_LIST(object);
	GvStationListPrivate *priv = self->priv;

	/* Initialize pathes */
	priv->load_pathes = gv_get_existing_path_list
	                    (GV_DIR_USER_CONFIG | GV_DIR_SYSTEM_CONFIG, "stations");
	priv->save_path = g_build_filename(gv_get_user_config_dir(), "stations", NULL);

	/* Chain up */
	G_OBJECT_CHAINUP_CONSTRUCTED(gv_station_list, object);
}

static void
gv_station_list_init(GvStationList *self)
{
	TRACE("%p", self);

	/* Initialize private pointer */
	self->priv = gv_station_list_get_instance_private(self);
}

static void
gv_station_list_class_init(GvStationListClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	TRACE("%p", class);

	/* Override GObject methods */
	object_class->finalize = gv_station_list_finalize;
	object_class->constructed = gv_station_list_constructed;

	/* Signals */
	signals[SIGNAL_LOADED] =
	        g_signal_new("loaded", G_TYPE_FROM_CLASS(class),
	                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			     G_TYPE_NONE, 0);

	signals[SIGNAL_STATION_ADDED] =
	        g_signal_new("station-added", G_TYPE_FROM_CLASS(class),
	                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			     G_TYPE_NONE, 1, G_TYPE_OBJECT);

	signals[SIGNAL_STATION_REMOVED] =
	        g_signal_new("station-removed", G_TYPE_FROM_CLASS(class),
	                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			     G_TYPE_NONE, 1, G_TYPE_OBJECT);

	signals[SIGNAL_STATION_MODIFIED] =
	        g_signal_new("station-modified", G_TYPE_FROM_CLASS(class),
	                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			     G_TYPE_NONE, 1, G_TYPE_OBJECT);

	signals[SIGNAL_STATION_MOVED] =
	        g_signal_new("station-moved", G_TYPE_FROM_CLASS(class),
	                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			     G_TYPE_NONE, 1, G_TYPE_OBJECT);
}
