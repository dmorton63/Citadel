// QWindowing Style type helpers
// Namespace: QW

#include "QWStyleTypes.h"
#include "QCString.h"

namespace QW
{
    namespace
    {
        struct RoleName
        {
            const char *name;
            ButtonRole role;
        };

        constexpr RoleName ROLE_NAMES[] = {
            {"default", ButtonRole::Default},
            {"accent", ButtonRole::Accent},
            {"sidebar", ButtonRole::Sidebar},
            {"sidebar-selected", ButtonRole::SidebarSelected},
            {"taskbar", ButtonRole::Taskbar},
            {"taskbar-active", ButtonRole::TaskbarActive},
            {"destructive", ButtonRole::Destructive},
        };

        inline char toLower(char c)
        {
            if (c >= 'A' && c <= 'Z')
                return static_cast<char>(c - 'A' + 'a');
            return c;
        }

        bool equalsIgnoreCase(const char *a, const char *b)
        {
            if (!a || !b)
                return false;

            for (QC::usize idx = 0;; ++idx)
            {
                const char ac = toLower(a[idx]);
                const char bc = toLower(b[idx]);
                if (ac != bc)
                    return false;
                if (ac == '\0')
                    return true;
            }
        }
    } // namespace

    const char *buttonRoleToString(ButtonRole role)
    {
        for (const auto &entry : ROLE_NAMES)
        {
            if (entry.role == role)
                return entry.name;
        }
        return ROLE_NAMES[0].name;
    }

    bool buttonRoleFromString(const char *text, ButtonRole *outRole)
    {
        if (!text || !outRole)
            return false;

        for (const auto &entry : ROLE_NAMES)
        {
            if (equalsIgnoreCase(text, entry.name))
            {
                *outRole = entry.role;
                return true;
            }
        }

        return false;
    }

} // namespace QW
