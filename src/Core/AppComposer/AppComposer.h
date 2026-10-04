#ifndef CLASSTOPLAND_NEXT_APPCOMPOSER_H
#define CLASSTOPLAND_NEXT_APPCOMPOSER_H
#include<QObject>
#include"../ProfileManager/ProfileManager.h"
#include"../Logger/Logger.h"
#include"../ConfigManager/ConfigManager.h"
#include"../../CourseBar/CourseBar.h"
#include<QDir>
#include<QMessageBox>

#define DATA_PATH (QDir::homePath()+"/ClassTopLand-Next_Data")
#define CONFIG_FILE_PATH (DATA_PATH+"/MainConfig.json")
#define PROFILES_PATH (DATA_PATH+"/profiles")
#define GET_PROFILE_PATH(profileName) (PROFILES_PATH+"/"+profileName+".json")
class AppComposer : public QObject
{
public:
    AppComposer(QObject *parent = nullptr);
    ~AppComposer();
    bool startApp();
private:
    bool initInstances();
    void dropInstances();
    void printLogo();
    bool initialized_ = false;
    CourseBar* courseBar;
};


#endif //CLASSTOPLAND_NEXT_APPCOMPOSER_H
