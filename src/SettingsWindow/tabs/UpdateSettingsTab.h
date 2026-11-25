#pragma once

#include <ElaScrollPage.h>

class UpdateService;

class UpdateSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit UpdateSettingsTab(UpdateService* update, QWidget* parent = nullptr);
};
