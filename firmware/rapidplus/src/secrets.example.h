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

#endif
