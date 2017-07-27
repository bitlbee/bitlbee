# TODO

1. Must implement `MASTODON_EVT_NOTIFICATION` in
   `mastodon_stream_handle_event`. See documentation for
   the
   [Streaming API](https://github.com/tootsuite/documentation/blob/master/Using-the-API/Streaming-API.md).

1. Rename `mastodon_user` to `mastodon_account`. Change `uid` to `id`.
   Change `screen_name` to `display_name`. Change `name` to `acct`.

1. When showing a status, decide how to handle the URLs
   of
   [media attachments](https://github.com/tootsuite/documentation/blob/master/Using-the-API/API.md#attachment).
   Probably use `"Attachments: "` followed by a comma-separated list
   of `type+" "+(text_url||remote_url)`.

1. We can delete `md->follow_ids`.

1. Add copyright statements.

1. Add documentation to the help files.

1. Remove all remaining Twitter code.

1. Write https://wiki.bitlbee.org/HowtoMastodon

1. Send Merge Request!!

1. Think about which commands to support in the future:

	- mute conversation
	- mute account
	- block account
	- report account
	- favorite status
	- boost status
	- reply to status
	- follow account
	- unfollow account
	- info about an account
	- handling of spoilers
	
