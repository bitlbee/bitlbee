# TODO

1. Muting reboosts for a user.

1. deduplicate mentions: 
    
	<neoanderthal> [1c->13] @neoanderthal mentioned you: @kensanata Sure -
		test your reports and I'll let you know how it's working.
	<neoanderthal> [1d->13] @kensanata Sure - test your reports and I'll
		let you know how it's working.

1. Get rid of the extra "You:" when showing notifications.
   
   <root> You: [14->0a] @kensanata@social.nasqueron.org boosted your
   status ...

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
