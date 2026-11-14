I actually really like that.

It's simple, predictable, and most importantly finishable.

Instead of trying to compete with Windows, KDE, or GNOME, Citadel gets a desktop that matches its current goals.

Citadel Desktop v1
Plain Text
+--------------------------------------------------+
|                                                  |
|                 Desktop Surface                  |
|                                                  |
|                                                  |
|                                                  |
|                                                  |
+--------------------------------------------------+
| Terminal | Explorer | Tasks | Clock | System ▼   |
+--------------------------------------------------+
Taskbar Buttons
Terminal

Launches the primary system terminal.

Plain Text
Terminal

Since much of Citadel administration is still command-driven, I'd treat this as a first-class application.

Explorer
Plain Text
Explorer

Provides:

File browsing
Drive information
Mounted volumes
Future network shares

Very Windows-like and easy for users to understand.

Tasks
Plain Text
Tasks
 

This could become one of Citadel's signature tools.

Initially:

Running applications
CPU usage
Memory usage
SMP status

Eventually:

Plain Text
CPU0: Active
CPU1: Active
CPU2: Active
CPU3: Active
 
TPM: Present
Secure Boot: Enabled

This becomes both a Task Manager and a System Monitor.

Clock
Plain Text
Clock

Simple.

Clicking it could later show:

Date
Time
Timezone
Uptime

No reason to overcomplicate it.

System
Plain Text
System ▼

Popup menu:

Plain Text
Shutdown
Hibernate
Restart
Update
Lock

Exactly the five options I'd start with.

Visually:

Plain Text
System ▼
 
┌─────────────┐
│ Shutdown    │
│ Hibernate   │
│ Restart     │
│ Update      │
│ Lock        │
└─────────────┘
One thing I'd strongly consider

Move Update above the power operations:

Plain Text
System ▼
 
┌─────────────┐
│ Update      │
│ Lock        │
│─────────────│
│ Restart     │
│ Hibernate   │
│ Shutdown    │
└─────────────┘

This groups actions by purpose:

Session

Update
Lock

Power

Restart
Hibernate
Shutdown

Users tend to find that more intuitive.

Color Scheme

If you're sticking with one theme:

Plain Text
Background : Dark Slate
Taskbar : Charcoal
Text : Light Gray
Accent : Citadel Blue
Warnings : Amber
Errors : Red

No theme engine.

No theme editor.

No customization UI.

Just:

Plain Text
Citadel Theme

until the OS is mature.

Honestly, given your roadmap:

Plain Text
SMP
TPM
Desktop Redesign
Secure Boot

I'd define this as the official Citadel desktop specification and freeze it. Every week spent building theme systems or desktop customization is a week not spent finishing TPM or Secure Boot.

A borderless desktop with a fixed bottom taskbar and five core buttons feels exactly right for a first-generation Citadel shell. It is small enough to complete, yet complete enough that a user can actually live in it.