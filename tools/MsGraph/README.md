# Microsoft Graph Demo

A read-only explorer for the [Microsoft Graph REST API](https://learn.microsoft.com/en-us/graph/overview),
built as a Ludo **tool script**. Open it from **Tools → MsGraph → Microsoft Graph Demo**.

Microsoft Graph exposes a single endpoint — `https://graph.microsoft.com` — with two
API versions:

- `v1.0` — general availability (GA). Updates are additive and non-breaking.
- `beta` — preview. Do not take a production dependency on it.

Request pattern: `https://graph.microsoft.com/v1.0/{resource}?[query_parameters]`

## Getting an access token

Every Graph call needs a Bearer token. For a demo, the quickest source is the
**Microsoft Graph Explorer** (no app registration required):

1. Open <https://developer.microsoft.com/en-us/graph/graph-explorer>
   (the tool shows this link in its **Get a token** row — click **Open** to
   launch it in your browser)
2. Sign in with a work/school or personal Microsoft account
3. Open the **Access token** tab and copy the token
4. Paste it into the **Access token** box in the tool, then use the tabs

Tokens expire in about an hour. When calls start returning **HTTP 401**, copy a
fresh token. Ludo decodes the JWT payload automatically as soon as the token is
pasted and shows the signed-in user, granted scopes, and expiry.

Graph Explorer's default consent covers every endpoint this demo calls.

## What the demo does

| Tab | Request | Scope |
|-----|---------|-------|
| Profile | `GET /me`, `/me/manager`, `/me/photo/$value` | `User.Read` |
| OneDrive | `GET /me/drive/root/children` | `Files.Read` |
| Mail | `GET /me/messages` | `Mail.Read` |
| Calendar | `GET /me/events` | `Calendars.Read` |
| Raw JSON | any path you type, e.g. `/me/memberOf` | depends |

Each tab renders results in a table; the **Raw JSON** tab pretty-prints the full
response body of the most recent request, whatever tab issued it.

This demo is deliberately read-only — it issues `GET` requests only.

## Notes

- The `$select`, `$top`, and `$orderby` query options are percent-encoded with
  `http.url_encode`, so values containing spaces (e.g. `receivedDateTime desc`)
  are sent safely.
- `string.match` on the JWT uses Lua patterns, not regex: `^[^.]+%.([^.]+)%.`
  splits the `header.payload.signature` token on literal dots.
- Useful references:
  - <https://learn.microsoft.com/en-us/graph/api/overview?view=graph-rest-1.0>
  - <https://developer.microsoft.com/en-us/graph/graph-explorer>
