# TODO

- help file
- oauth URL: octodon.social/oauth/token?scope=read write follow&response_type=code&redirect_uri=octodon.social/auth/sign_in&client_id=
    - encode spaces
	- lacks https
	- lacks client_id
	
 - mastodon_buddy_msg -> oauth2_get_refresh_token -> oauth2_access_token -> return req != NULL
   What is going on, here? 
   http_dorequest is being called with oauth2_got_token as callback (cb->func);
   but actually then oauth2_access_token_done is passed as func and cb_data is passed as data,
   which is then stored in req->data.
   So the http_input_function func is actually oauth2_access_token_done,
   which is then stored in req->func.
   But nothing actually happens with it back in oauth2_access_token!

(gdb) p parsed.u.object.values[0]
$7 = {name = 0x100928ce0 "id", name_length = 2, value = 0x100928ea0}
(gdb) p parsed.u.object.values[1]
$8 = {name = 0x100928ce3 "redirect_uri", name_length = 12, value = 0x100928ed0}
(gdb) p parsed.u.object.values[2]
$9 = {name = 0x100928cf0 "client_id", name_length = 9, value = 0x100929650}
(gdb) p parsed.u.object.values[3]
$10 = {name = 0x100928cfa "client_secret", name_length = 13, value = 0x100929680}
(gdb) p parsed.u.object.values[2].type
There is no member named type.
(gdb) p parsed.u.object.values[2].value
$11 = (struct _json_value *) 0x100929650
(gdb) p parsed.u.object.values[2].value.u
$12 = {boolean = 64, integer = 64, dbl = 3.1620201333839779e-322, string = {length = 64, 
    ptr = 0x100929310 "dd68216dd15bc18ce464309855eb2b614f260c3c96bad4a2e4c60c50f6a134f8"}, object = {length = 64, values = 0x100929310}, array = {
    length = 64, values = 0x100929310}}
(gdb) p parsed.u.object.values[2].value.u.type
There is no member named type.
(gdb) p parsed.u.object.values[2].value.type
$13 = json_string
(gdb) p parsed.u.object.values[2].value.u.string
$14 = {length = 64, ptr = 0x100929310 "dd68216dd15bc18ce464309855eb2b614f260c3c96bad4a2e4c60c50f6a134f8"}
(gdb) p parsed.u.object.values[3].value.u.string
$15 = {length = 64, ptr = 0x100929500 "d2018abcad37d83b8c54cdeaf0da47413ab71b42426311b4c499bf6ad6c6c1e3"}
(gdb) p parsed.u.object.values[1].value.u.string
$16 = {length = 25, ptr = 0x1009296b0 "urn:ietf:wg:oauth:2.0:oob"}
(gdb) p parsed.u.object.values[0].value.u.string
$17 = {length = 3378, ptr = 0x0}
(gdb) p parsed.u.object.values[0].value.type
$18 = json_integer
(gdb) p parsed.u.object.values[0].value.u.integer
$19 = 3378


Well, client_id and client_secret are now saved so the next question is: how to authenticate?

17:41 <mastodon_oauth> Open this URL in your browser to authenticate:
    octodon.social/oauth/token?scope=read write
    follow&response_type=code&redirect_uri=octodon.social/auth/sign_in&client_id=4d9777e05f5300007ff97b3e8b0c109cbc74b21779058dd1159ebbaac49052c9
17:41 <mastodon_oauth> Respond to this message with the returned
    authorization token.

https://octodon.social/oauth/token?scope=read%20write%20follow&response_type=code&redirect_uri=octodon.social/auth/sign_in&client_id=4d9777e05f5300007ff97b3e8b0c109cbc74b21779058dd1159ebbaac49052c9
=> The page you were looking for doesn't exist.

https://octodon.social/oauth/authorize?scope=read%20write%20follow&response_type=code&redirect_uri=octodon.social/auth/sign_in&client_id=4d9777e05f5300007ff97b3e8b0c109cbc74b21779058dd1159ebbaac49052c9
=> The redirect uri included is not valid.

https://octodon.social/oauth/authorize?scope=read%20write%20follow&response_type=code&redirect_uri=urn:ietf:wg:oauth:2.0:oob&client_id=4d9777e05f5300007ff97b3e8b0c109cbc74b21779058dd1159ebbaac49052c9
-> YES

Type the number: 02d093b185d5e200886955ec9ae72e671110daf2238beef237fc5566980f78f7

But: invalid_grant

POST /oauth/token HTTP/1.0
Host: octodon.social
Content-Type: application/x-www-form-urlencoded
Content-Length: 298

client_id=4d9777e05f5300007ff97b3e8b0c109cbc74b21779058dd1159ebbaac49052c9&client_secret=a22582c07513e193e8ddc41b44cf76f5e8987b1049187e9515fb45cc64fd18db&code=02d093b185d5e200886955ec9ae72e671110daf2238beef237fc5566980f78f7&grant_type=authorization_code&redirect_uri=octodon.social%2Fauth%2Fsign_in



https://github.com/tootsuite/documentation/blob/master/Using-the-API/Streaming-API.md



14:16 <root> mastodon - Logging in: Getting contact list
14:16 <root> mastodon - Logging in: Error: Could not retrieve
    /api/v1/friends/ids.json: 404 Not Found
14:16 <root> mastodon - Logging in: Signing off..
14:16 *** octodonsocial_kensanata QUIT bitlbee.localhost mastodon.localhost
14:16 <root> mastodon - Logging in: Reconnecting in 45 seconds..
