// Template for src/secrets.h (which is GITIGNORED). Copy this file to src/secrets.h and fill
// in the real upload endpoints + credentials. Do NOT put real values here — THIS file is
// committed. The build includes src/secrets.h; without it, compilation fails on purpose so a
// missing secrets file is obvious rather than silently shipping placeholders.
#ifndef _SECRETS_H
#define _SECRETS_H

#define SECRET_GAS_URL "https://script.google.com/macros/s/PASTE_DEPLOYMENT_ID/exec"
#define SECRET_INGEST_URL "https://your-ingest-host/ingest"
#define SECRET_INGEST_TOKEN "PASTE_INGEST_BEARER_TOKEN"
#define SECRET_ERP_URL "https://your-erp-host/api/v1/results/ingest"
#define SECRET_ERP_TOKEN "PASTE_ERP_X_API_KEY"
// OTA moved off GitHub onto the ingest server in v2.4.4. Same host, same Bearer as
// SECRET_INGEST_TOKEN - no new credential. The .bin URL is NOT here: /ota/check returns
// an ABSOLUTE one built from the request, so whichever host answered also serves the
// download - the fallback below needs no second .bin constant and cannot cross hosts.
//
// Two hosts, tried in order. The second one exists because OTA is the ONLY way to push a
// fix to a fielded machine: a firmware that knows a single host turns any outage of that
// host into a fleet nobody can reach. Point them at two independent front doors of the
// SAME server; leave them equal only if there genuinely is just one way in.
#define SECRET_OTA_CHECK_URL "https://your-ingest-host/ota/check"
#define SECRET_OTA_CHECK_URL_FALLBACK "https://your-backup-host/ota/check"

#endif
