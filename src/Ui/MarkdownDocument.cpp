#include "MarkdownDocument.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QVariant>

namespace AppUi {

void MarkdownDocument::setSource(const QString& source)
{
    setMarkdown(source, MarkdownFeatures(MarkdownDialectGitHub) | MarkdownNoHTML);

    // The generated HTML can also be rendered by a QLabel with its own document.
    // Remove image formats before exporting it to prevent resource lookups there.
    QList<QPair<int, int>> images;
    for (auto block = begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isImageFormat())
                images.append({fragment.position(), fragment.length()});
        }
    for (auto it = images.crbegin(); it != images.crend(); ++it) {
        QTextCursor cursor(this);
        cursor.setPosition(it->first);
        cursor.setPosition(it->first + it->second, QTextCursor::KeepAnchor);
        cursor.insertText(QStringLiteral("[图片]"), QTextCharFormat());
    }
}

QVariant MarkdownDocument::loadResource(int, const QUrl&)
{
    return {};
}

}
