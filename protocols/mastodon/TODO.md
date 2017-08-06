# TODO

1. What about `mastodon_get_info` for just a nick like
   `octodonsocial_kensanata`? At the moment it gets searched and isn't
   found, so no output. I'd like to simply call `mastodon_instance` in
   this case.

1. Muting reboosts for a user.

1. deduplicate mentions: 
    
	<neoanderthal> [1c->13] @neoanderthal mentioned you: @kensanata Sure -
		test your reports and I'll let you know how it's working.
	<neoanderthal> [1d->13] @kensanata Sure - test your reports and I'll
		let you know how it's working.

1. Get rid of the extra "You:" when showing notifications.
   
   <root> You: [14->0a] @kensanata@social.nasqueron.org boosted your
   status ...

1. testing
    - `unfollow nick` ✓
	- `follow name@some.instance` ✓
	- `follow nick` ✓ (if the nick is already in the channel!)
	- posting ✓
	- posting implicit replies to toots older than 3h ✓
	- posting implicit replies to newer toots ✓
	- `reply id` to post an explicit replies to newer toots ✓
	- `undo id` to delete a toot ✓
	- `undo` to delete latest toot with no other trickery ✓
	- `fav id` and `fav nick` to favor a toot ✓ (and all synomyms)
	- `unfav id` and `unfav nick` to unfavor a toot ✓ (and all synomyms)
	- `block nick` to block a user ✓
	- `unblock nick` to unblock a user ✓
	- `mute user nick` to mute a user ✓
	- `unmute user nick` to unmute a user ✓
	- `mute id` to mute a conversation ✓ (but hard to prove?)
	- `unmute id` to unmute a conversation ✓ (but hard to prove?)
	- `boost id` and `boost nick` to boost a toot ✓
	- `unboost id` and `unboost nick` to unboost a toot ✓ (but caching
      requires me to visit the `/web/statuses/<id>/reblogs` page)
    - `url id` to show the URL of a status ✓
	- `info` shows how to use it ✓
	- `info instance` shows debug info for the instance ✓
	- `info [id|nick]` shows debug info for a status ✓
	- `info user [nick|account]` shows debug info for a user ✓

1. Also had one crash in `mastodon_log (ic=0x10090b270,
    format=0x100092027 "Command processed successfully")` at
    `mastodon.c:1137` where `md->timeline_gc` results in `Cannot
    access memory at address`. What now? I was unable to reproduce it.

1. test muting of conversations

1. When showing a status, decide how to handle the URLs of [media
   attachments](https://github.com/tootsuite/documentation/blob/master/Using-the-API/API.md#attachment).
   Probably use `"Attachments: "` followed by a comma-separated list
   of `type+" "+(text_url||remote_url)`. Deduplicate if the URL is
   also in the text itself!

1. Remove all remaining Twitter code.

1. Write https://wiki.bitlbee.org/HowtoMastodon

1. Send Merge Request!!

1. Think about which commands to support in the future:

	- handling of spoilers
	- tracking hashtags

1. Real undo/redo implementation.

1. Go through all the root commands and make them call the appropriate
   functions we already have. At the moment there are many empty
   implementations and that's no good. Compare `cmd_block` which calls
   functions defined in `struct prpl` which are defined in
   `mastodon_initmodule`.

    - `mastodon_buddy_msg` ✓
	- `mastodon_get_info` ✓
    - `mastodon_add_buddy` ✓
	- `mastodon_remove_buddy` ✓
	- `mastodon_chat_msg` ✓
	- `mastodon_chat_invite` was deleted ✓
	- `mastodon_chat_join` involves filters?
	- `mastodon_chat_leave` ✓
	- `mastodon_keepalive` was deleted ✓
	- `mastodon_add_permit` is required but empty ✓
	- `mastodon_rem_permit` is required but empty ✓
	- `mastodon_add_deny` ✓
	- `mastodon_rem_deny` ✓

