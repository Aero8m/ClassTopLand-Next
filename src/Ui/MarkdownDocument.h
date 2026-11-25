#pragma once

#include <QTextDocument>

namespace AppUi {

class MarkdownDocument final : public QTextDocument
{
public:
    void setSource(const QString& source);

protected:
    QVariant loadResource(int type, const QUrl& url) override;
};

}
