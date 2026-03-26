⭐ CITADEL UI RUNTIME — MASTER TODO LIST (Ordered Execution Plan)
PHASE 1 — Foundations
1. Define JSON Schema
- Window header structure
- Window block
- Controls block
- Bindings block
- Security block
2. Build the Universal JSON Loader
- Open file
- Parse JSON
- Validate schema
- Instantiate controls
- Register events
- Return Window object

PHASE 2 — Security Pipeline
3. Implement DEBUG vs PRODUCTION Modes
- DEBUG → editable, never protected
- PRODUCTION → must be signed
4. Implement Security Center Validation
- Validate JSON structure
- Validate control types
- Validate fields
- Reject malicious patterns
5. Implement Signing + Hashing
- TPM-backed hashing
- Signature block
- Move PRODUCTION files to protected storage

PHASE 3 — WindowManager Integration
6. Add LOAD Flags
- LOAD_ALWAYS
- LOAD_ON_USE
- LOAD_BY_CMD
7. Implement String Templating
- Replace {0} with supplied parameter
- Default fallback if none supplied
8. Implement Override System
- Load system JSON
- Load user JSON override
- Merge fields
- Produce final window definition

PHASE 4 — Permissions Model
9. Implement UsagePermissions in JSON
- SYSTEM_CMD
- ADMIN_CMD
- USER_CMD
- RESTRICTED_CMD
10. Add Command Metadata
Each command declares:
- required permission level
11. Add Command Center Enforcement
Check:
- user profile role
- terminal UsagePermissions
- command required level
If allowed → execute
If not → deny

PHASE 5 — Controls + Terminal
12. Build QUButton.json
- geometry
- visual states
- events
- style class
13. Build QJSYSTerminal.json (System Terminal)
- titlebar
- close button
- output area
- input box
- scroll bar
- save button
- UsagePermissions: SYSTEM_CMD
14. Build QJTerminal.json (User Override)
- override title
- override UsagePermissions: USER_CMD

PHASE 6 — Integration + Testing
15. Implement Command: cmd
- Loads system terminal
- Applies user override
- Applies permissions
- Displays final window
16. Test All Permission Levels
- System
- Admin
- User
- Restricted
17. Test Signing + Protected Storage
- Promote JSON to PRODUCTION
- Validate signature
- Load from protected storage
