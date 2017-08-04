# TODO


1. deduplicate mentions: 
    
	18:01 <neoanderthal> [1c->13] @neoanderthal mentioned you: @kensanata Sure -
		test your reports and I'll let you know how it's working.
	18:01 <neoanderthal> [1d->13] @kensanata Sure - test your reports and I'll
		let you know how it's working.

1. testing
    - `unfollow nick` works
	- `follow nick@some.instance` works
	- posting works
	- posting implicit replies to toots older than 3h works
	- posting implicit replies to newer toots works
	- `reply n` to post an explicit replies to newer toots works
	- `undo n` to delete a toot works
	- `undo` to delete latest toot with no other trickery works
	- `fav n` to favor a toot works (and all synomyms)
	- `unfav n` to unfavor a toot works (and all synomyms)

1. test muting of conversations

1. When showing a status, decide how to handle the URLs
   of
   [media attachments](https://github.com/tootsuite/documentation/blob/master/Using-the-API/API.md#attachment).
   Probably use `"Attachments: "` followed by a comma-separated list
   of `type+" "+(text_url||remote_url)`.

1. We can delete `md->follow_ids`.

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
	- follow account
	- unfollow account
	- info about an account
	- handling of spoilers
	- tracking hashtags
