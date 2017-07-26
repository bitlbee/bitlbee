# TODO

1. Must implement `MASTODON_EVT_NOTIFICATION` in
   `mastodon_stream_handle_event`. See documentation for
   the
   [Streaming API](https://github.com/tootsuite/documentation/blob/master/Using-the-API/Streaming-API.md).

1. We can delete `md->follow_ids`, the settings `_last_tweet`, `oauth`
   (since it's mandatory), `fetch_interval` (since we're using the
   streaming API).

1. The API to get the accounts we follow doesn't seem to paginate
   correctly using `since_id`; it keeps returning data with ids below
   the parameter.

1. Add copyright statements.

1. Add documentation to the help files.

1. Remove all remaining Twitter code.

1. Write https://wiki.bitlbee.org/HowtoMastodon
