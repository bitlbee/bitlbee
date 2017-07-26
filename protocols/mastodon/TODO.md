# TODO

1. Documentation for the [Streaming API](https://github.com/tootsuite/documentation/blob/master/Using-the-API/Streaming-API.md)

1. We can delete md->follow_ids

1. The API to get the accounts we follow doesn't seem to paginate
   correctly using `since_id`; it keeps returning data with ids below
   the parameter.

1. Must implement `MASTODON_EVT_NOTIFICATION` in
   `mastodon_stream_handle_event`.

1. Add copyright statements.
