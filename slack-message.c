#include <debug.h>

#include "slack-json.h"
#include "slack-rtm.h"
#include "slack-user.h"
#include "slack-channel.h"
#include "slack-message.h"

void slack_message(SlackAccount *sa, json_value *json) {
	const char *user_id    = json_get_prop_strptr(json, "user");
	const char *channel_id = json_get_prop_strptr(json, "channel");
	const char *text       = json_get_prop_strptr(json, "text");
	const char *subtype    = json_get_prop_strptr(json, "subtype");

	time_t mt = slack_parse_time(json_get_prop(json, "ts"));

	PurpleMessageFlags flags = PURPLE_MESSAGE_RECV;
	if (subtype)
		flags |= PURPLE_MESSAGE_SYSTEM; /* PURPLE_MESSAGE_NOTIFY? */
	if (json_get_prop_boolean(json, "hidden", FALSE))
		flags |= PURPLE_MESSAGE_INVISIBLE;

	SlackUser *user = (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id);
	SlackChannel *chan;
	if (user && slack_object_id_is(user->im, channel_id)) {
		/* IM */
		serv_got_im(sa->gc, user->name, text, flags, mt);
	} else if ((chan = (SlackChannel*)slack_object_hash_table_lookup(sa->channels, channel_id))) {
		/* Channel */
		if (!chan->cid)
			return;

		PurpleConvChat *conv;
		if (subtype && (conv = slack_channel_get_conversation(sa, chan))) {
			if (!strcmp(subtype, "channel_topic") ||
					!strcmp(subtype, "group_topic"))
				purple_conv_chat_set_topic(conv, user ? user->name : user_id, json_get_prop_strptr(json, "topic"));
		}

		serv_got_chat_in(sa->gc, chan->cid, user ? user->name : user_id ?: "", flags, text, mt);
	} else {
		purple_debug_warning("slack", "Unhandled message: %s@%s: %s\n", user_id, channel_id, text);
	}
}

void slack_user_typing(SlackAccount *sa, json_value *json) {
	const char *user_id    = json_get_prop_strptr(json, "user");
	const char *channel_id = json_get_prop_strptr(json, "channel");

	SlackUser *user = (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id);
	SlackChannel *chan;
	if (user && slack_object_id_is(user->im, channel_id)) {
		/* IM */
		serv_got_typing(sa->gc, user->name, 3, PURPLE_TYPING);
	} else if ((chan = (SlackChannel*)slack_object_hash_table_lookup(sa->channels, channel_id))) {
		/* Channel */
		/* libpurple does not support chat typing indicators */
	} else {
		purple_debug_warning("slack", "Unhandled typing: %s@%s\n", user_id, channel_id);
	}
}

unsigned int slack_send_typing(PurpleConnection *gc, const char *who, PurpleTypingState state) {
	SlackAccount *sa = gc->proto_data;

	if (state != PURPLE_TYPING)
		return 0;

	SlackUser *user = g_hash_table_lookup(sa->user_names, who);
	if (!user || !*user->im)
		return 0;

	GString *channel = append_json_string(g_string_new(NULL), user->im);
	slack_rtm_send(sa, NULL, NULL, "typing", "channel", channel->str, NULL);
	g_string_free(channel, TRUE);

	return 3;
}

void slack_member_joined_channel(SlackAccount *sa, json_value *json, gboolean joined) {
	SlackChannel *chan = (SlackChannel*)slack_object_hash_table_lookup(sa->channels, json_get_prop_strptr(json, "channel"));
	if (!chan)
		return;

	PurpleConvChat *conv = slack_channel_get_conversation(sa, chan);
	if (!conv)
		return;

	const char *user_id = json_get_prop_strptr(json, "user");
	SlackUser *user = (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id);
	if (joined)
		purple_conv_chat_add_user(conv, user ? user->name : user_id, NULL, PURPLE_CBFLAGS_VOICE, TRUE);
	else
		purple_conv_chat_remove_user(conv, user ? user->name : user_id, NULL);
}
