#ifndef CLASSTOPLAND_NEXT_ABOUTDIALOG_H
#define CLASSTOPLAND_NEXT_ABOUTDIALOG_H

#include <ElaDialog.h>

class UpdateService;

class AboutDialog final : public ElaDialog
{
public:
    explicit AboutDialog(UpdateService* update, QWidget* parent = nullptr);
};

#endif
