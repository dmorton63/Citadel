#pragma once

// QDesktop HTML engine - HTML-flavored controls that wrap system controls
// Namespace: QD::Html

#include "QCTypes.h"
#include "QCVector.h"

namespace QW
{
    class Window;
}

namespace QW::Controls
{
    class Panel;
    class IControl;
}

namespace QD::Html
{

    struct ImageAsset;

    using LinkClickHandler = void (*)(const char *href, void *userData);

    class Document
    {
    public:
        Document();
        ~Document();

        void clear();

        // Render a minimal HTML subset into `root` (children are created by this engine).
        // Supported tags: html, head, body, link(rel=stylesheet), style, div, p, h1..h6, br, img (src), a (href), input(type=text), button.
        // CSS support is a small deterministic subset (element / .class / #id selectors + inline style attributes).
        void renderTo(QW::Window *window, QW::Controls::Panel *root, const char *htmlText);

        // Fetch and render a remote document over plain HTTP (no TLS).
        // Only supports URLs of the form: http://host/path
        void renderUrlTo(QW::Window *window, QW::Controls::Panel *root, const char *url);

        const QC::Vector<QW::Controls::IControl *> &nativeControls() const { return m_native; }
        QC::u32 contentHeight() const { return m_contentHeight; }

        void setLinkClickHandler(LinkClickHandler handler, void *userData);

    private:
        struct Node;

        void clearInternal(bool detachFromParent);
        void layout(QW::Window *window, QW::Controls::Panel *root);
        void renderToInternal(QW::Window *window, QW::Controls::Panel *root, const char *htmlText);

        LinkClickHandler m_linkHandler = nullptr;
        void *m_linkUserData = nullptr;

        Node *m_root = nullptr;

        QC::Vector<QW::Controls::IControl *> m_native;
        QC::Vector<ImageAsset *> m_images;
        QC::u32 m_contentHeight = 0;

        // Base URL used to resolve relative href/src when rendering remote docs.
        // Empty when rendering local files.
        char m_baseUrl[256] = {0};
    };

} // namespace QD::Html
