#include "settingspage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <QCompleter>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QSignalBlocker>
#include <QString>

#include <components/config/gamesettings.hpp>
#include <components/files/qtconversion.hpp>
#include <components/settings/values.hpp>

#include "utils/openalutil.hpp"

namespace
{
    void loadSettingBool(const Settings::SettingValue<bool>& value, QCheckBox& checkbox)
    {
        checkbox.setCheckState(value ? Qt::Checked : Qt::Unchecked);
    }

    void loadSettingFloat(const Settings::SettingValue<float>& value, QDoubleSpinBox& spinBox)
    {
        spinBox.setValue(value);
    }

    void saveSettingBool(const QCheckBox& checkbox, Settings::SettingValue<bool>& value)
    {
        value.set(checkbox.checkState() == Qt::Checked);
    }

    void saveSettingFloat(const QDoubleSpinBox& spinBox, Settings::SettingValue<float>& value)
    {
        value.set(static_cast<float>(spinBox.value()));
    }

    void loadSettingInt(const Settings::SettingValue<int>& value, QComboBox& comboBox)
    {
        comboBox.setCurrentIndex(value);
    }

    void loadSettingInt(const Settings::SettingValue<DetourNavigator::CollisionShapeType>& value, QComboBox& comboBox)
    {
        comboBox.setCurrentIndex(static_cast<int>(value.get()));
    }

    void saveSettingInt(const QComboBox& comboBox, Settings::SettingValue<int>& value)
    {
        value.set(comboBox.currentIndex());
    }

    void saveSettingInt(const QComboBox& comboBox, Settings::SettingValue<DetourNavigator::CollisionShapeType>& value)
    {
        value.set(static_cast<DetourNavigator::CollisionShapeType>(comboBox.currentIndex()));
    }

    void loadSettingInt(const Settings::SettingValue<int>& value, QSpinBox& spinBox)
    {
        spinBox.setValue(value);
    }

    void saveSettingInt(const QSpinBox& spinBox, Settings::SettingValue<int>& value)
    {
        value.set(spinBox.value());
    }

    int toIndex(Settings::HrtfMode value)
    {
        switch (value)
        {
            case Settings::HrtfMode::Auto:
                return 0;
            case Settings::HrtfMode::Disable:
                return 1;
            case Settings::HrtfMode::Enable:
                return 2;
        }
        return 0;
    }

    int toIndexControllerIconStyle(const std::string& value)
    {
        if (value == "steam")
            return 1;
        if (value == "xbox")
            return 2;
        if (value == "playstation")
            return 3;
        if (value == "switch")
            return 4;
        if (value == "gamecube")
            return 5;
        return 0;
    }

    std::string fromIndexControllerIconStyle(int index)
    {
        switch (index)
        {
            case 1:
                return "steam";
            case 2:
                return "xbox";
            case 3:
                return "playstation";
            case 4:
                return "switch";
            case 5:
                return "gamecube";
            default:
                return "auto";
        }
    }

    struct RecommendedScalingDefaults
    {
        int width;
        int height;
        double interfaceScaling;
        double dialogueScaling;
        double settingsScaling;
    };

    constexpr std::array<RecommendedScalingDefaults, 21> recommendedScalingDefaults = {
        RecommendedScalingDefaults{ 640, 480, 1.0, 1.0, 0.85 },
        RecommendedScalingDefaults{ 720, 480, 1.0, 1.0, 0.85 },
        RecommendedScalingDefaults{ 720, 576, 1.0, 1.2, 1.0 },
        RecommendedScalingDefaults{ 800, 600, 1.2, 1.4, 1.0 },
        RecommendedScalingDefaults{ 1024, 768, 1.25, 1.45, 1.05 },
        RecommendedScalingDefaults{ 1152, 864, 1.45, 1.65, 1.25 },
        RecommendedScalingDefaults{ 1280, 720, 1.45, 1.65, 1.25 },
        RecommendedScalingDefaults{ 1280, 800, 1.65, 2.0, 1.4 },
        RecommendedScalingDefaults{ 1280, 960, 1.65, 2.0, 1.4 },
        RecommendedScalingDefaults{ 1280, 1024, 1.65, 2.0, 1.4 },
        RecommendedScalingDefaults{ 1360, 768, 1.55, 1.85, 1.3 },
        RecommendedScalingDefaults{ 1366, 768, 1.55, 1.85, 1.3 },
        RecommendedScalingDefaults{ 1440, 1080, 1.85, 2.15, 1.55 },
        RecommendedScalingDefaults{ 1600, 900, 1.75, 2.05, 1.5 },
        RecommendedScalingDefaults{ 1600, 1200, 1.95, 2.35, 1.65 },
        RecommendedScalingDefaults{ 1680, 1050, 1.85, 2.25, 1.55 },
        RecommendedScalingDefaults{ 1920, 1080, 1.85, 2.25, 1.55 },
        RecommendedScalingDefaults{ 1920, 1200, 1.95, 2.35, 1.65 },
        RecommendedScalingDefaults{ 1920, 1440, 2.35, 2.75, 2.0 },
        RecommendedScalingDefaults{ 2560, 1440, 2.35, 2.75, 2.0 },
        RecommendedScalingDefaults{ 3840, 2160, 2.65, 3.15, 2.25 },
    };

    bool tryGetRecommendedScaling(
        int width, int height, double& interfaceScaling, double& dialogueScaling, double& settingsScaling)
    {
        for (const auto& entry : recommendedScalingDefaults)
        {
            if (entry.width == width && entry.height == height)
            {
                interfaceScaling = entry.interfaceScaling;
                dialogueScaling = entry.dialogueScaling;
                settingsScaling = entry.settingsScaling;
                return true;
            }
        }
        return false;
    }

    enum FileTypeRoles
    {
        Role_ThisFile = Qt::ItemDataRole::UserRole,
        Role_IsMainUserConfigDirectory,
        Role_ConfigDirectory,
        Role_LauncherLog,
        Role_OpenMWCfg,
        Role_OpenMWLog,
        Role_OpenMWCSLog,
        Role_SettingsCfg,
    };

    struct FileType
    {
        FileTypeRoles itemDataRole;
        const char* name;
        bool showInAllConfigDirectories;
    };

    const std::array configDirectoryFiles{
        FileType{ Role_LauncherLog, "launcher.log", false },
        FileType{ Role_OpenMWCfg, "openmw.cfg", true },
        FileType{ Role_OpenMWLog, "openmw.log", false },
        FileType{ Role_OpenMWCSLog, "openmw-cs.log", false },
        FileType{ Role_SettingsCfg, "settings.cfg", true },
    };
}

Launcher::SettingsPage::SettingsPage(
    const Files::ConfigurationManager& configurationManager, Config::GameSettings& gameSettings, QWidget* parent)
    : QWidget(parent)
    , mCfgMgr(configurationManager)
    , mGameSettings(gameSettings)
{
    setObjectName("SettingsPage");
    setupUi(this);

    for (const std::string& name : Launcher::enumerateOpenALDevices())
    {
        audioDeviceSelectorComboBox->addItem(QString::fromStdString(name), QString::fromStdString(name));
    }
    for (const std::string& name : Launcher::enumerateOpenALDevicesHrtf())
    {
        hrtfProfileSelectorComboBox->addItem(QString::fromStdString(name), QString::fromStdString(name));
    }

    loadSettings();
    connect(scalingSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
        &SettingsPage::onInterfaceScalingChanged);
    connect(dialogueScalingSpinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
        &SettingsPage::onDialogueScalingChanged);
    connect(settingsInterfaceScalingSpinBox,
        static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
        &SettingsPage::onSettingsInterfaceScalingChanged);
    mLastInterfaceScaling = scalingSpinBox->value();

    mCellNameCompleter.setModel(&mCellNameCompleterModel);
    startDefaultCharacterAtField->setCompleter(&mCellNameCompleter);

    connect(configsList, &QTreeWidget::itemActivated, this, &SettingsPage::slotOpenFile);

    auto actionOpenDir = new QAction(tr("Open Directory"), configsList);
    connect(actionOpenDir, &QAction::triggered, [this]() {
        QUrl configFolderUrl = configsList->currentItem()->data(0, Role_ConfigDirectory).toUrl();
        QDesktopServices::openUrl(configFolderUrl);
    });

    QList<QAction*> openFileActions;
    openFileActions.reserve(configDirectoryFiles.size());
    for (const auto& fileType : configDirectoryFiles)
    {
        QAction* action = new QAction(tr("Open %1").arg(fileType.name), configsList);
        connect(action, &QAction::triggered, [this, role = fileType.itemDataRole]() {
            QVariant fileUrl = configsList->currentItem()->data(0, role);
            if (fileUrl.isValid())
                QDesktopServices::openUrl(fileUrl.toUrl());
        });
        openFileActions.push_back(action);
    }

    connect(configsList, &QTreeWidget::customContextMenuRequested, [=, this](const QPoint& pos) {
        if (configsList->currentItem())
        {
            QMenu contextMenu;

            contextMenu.addAction(actionOpenDir);

            bool topLevel = !configsList->currentItem()->parent();

            for (qsizetype i = 0; i < openFileActions.size(); ++i)
            {
                if (configsList->currentItem()->data(0, Role_IsMainUserConfigDirectory).toBool()
                    || configDirectoryFiles[i].showInAllConfigDirectories)
                {
                    QVariant fileUrl = configsList->currentItem()->data(0, configDirectoryFiles[i].itemDataRole);
                    bool fileExists = fileUrl.isValid();
                    openFileActions[i]->setEnabled(fileExists);
                    openFileActions[i]->setVisible(topLevel || fileExists);
                    contextMenu.addAction(openFileActions[i]);
                }
            }

            contextMenu.exec(configsList->mapToGlobal(pos));
        }
    });
}

void Launcher::SettingsPage::loadCellsForAutocomplete(QStringList cellNames)
{
    // Update the list of suggestions for the "Start default character at" field
    mCellNameCompleterModel.setStringList(cellNames);
    mCellNameCompleter.setCompletionMode(QCompleter::PopupCompletion);
    mCellNameCompleter.setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
}

void Launcher::SettingsPage::on_skipMenuCheckBox_stateChanged(int state)
{
    startDefaultCharacterAtLabel->setEnabled(state == Qt::Checked);
    startDefaultCharacterAtField->setEnabled(state == Qt::Checked);
}

void Launcher::SettingsPage::on_recommendedScalingDefaultsCheckBox_stateChanged(int state)
{
    if (state == Qt::Checked)
    {
        int width = mLastResolutionWidth;
        int height = mLastResolutionHeight;
        if (width <= 0 || height <= 0)
        {
            width = Settings::video().mResolutionX;
            height = Settings::video().mResolutionY;
        }
        if (applyRecommendedScalingDefaults(width, height))
        {
            mLastRecommendedResolutionWidth = width;
            mLastRecommendedResolutionHeight = height;
        }
    }
    else
        setScalingValues(1.0, 1.0, 1.0);
}

void Launcher::SettingsPage::onResolutionChanged(int width, int height)
{
    mLastResolutionWidth = width;
    mLastResolutionHeight = height;
    if (recommendedScalingDefaultsCheckBox->checkState() == Qt::Checked
        && (width != mLastRecommendedResolutionWidth || height != mLastRecommendedResolutionHeight))
    {
        if (applyRecommendedScalingDefaults(width, height))
        {
            mLastRecommendedResolutionWidth = width;
            mLastRecommendedResolutionHeight = height;
        }
    }
}

void Launcher::SettingsPage::on_runScriptAfterStartupBrowseButton_clicked()
{
    QString scriptFile = QFileDialog::getOpenFileName(
        this, QObject::tr("Select script file"), QDir::currentPath(), QString(tr("Text file (*.txt)")));

    if (scriptFile.isEmpty())
        return;

    QFileInfo info(scriptFile);

    if (!info.exists() || !info.isReadable())
        return;

    const QString path(QDir::toNativeSeparators(info.absoluteFilePath()));
    runScriptAfterStartupField->setText(path);
}

namespace
{
    constexpr double cellSizeInUnits = 8192;

    double convertToCells(double unitRadius)
    {
        return unitRadius / cellSizeInUnits;
    }

    int convertToUnits(double cellGridRadius)
    {
        return static_cast<int>(cellSizeInUnits * cellGridRadius);
    }
}

bool Launcher::SettingsPage::loadSettings()
{
    // Game mechanics
    {
        loadSettingBool(Settings::game().mCanLootDuringDeathAnimation, *canLootDuringDeathAnimationCheckBox);
        loadSettingBool(Settings::game().mFollowersAttackOnSight, *followersAttackOnSightCheckBox);
        loadSettingBool(Settings::game().mRebalanceSoulGemValues, *rebalanceSoulGemValuesCheckBox);
        loadSettingBool(Settings::game().mEnchantedWeaponsAreMagical, *enchantedWeaponsMagicalCheckBox);
        loadSettingBool(
            Settings::game().mBarterDispositionChangeIsPermanent, *permanentBarterDispositionChangeCheckBox);
        loadSettingBool(Settings::game().mClassicReflectedAbsorbSpellsBehavior, *classicReflectedAbsorbSpellsCheckBox);
        loadSettingBool(Settings::game().mClassicCalmSpellsBehavior, *classicCalmSpellsCheckBox);
        loadSettingBool(
            Settings::game().mOnlyAppropriateAmmunitionBypassesResistance, *requireAppropriateAmmunitionCheckBox);
        loadSettingBool(Settings::game().mUncappedDamageFatigue, *uncappedDamageFatigueCheckBox);
        loadSettingBool(Settings::game().mNormaliseRaceSpeed, *normaliseRaceSpeedCheckBox);
        loadSettingBool(Settings::game().mSwimUpwardCorrection, *swimUpwardCorrectionCheckBox);
        loadSettingBool(Settings::game().mNPCsAvoidCollisions, *avoidCollisionsCheckBox);
        loadSettingInt(Settings::game().mStrengthInfluencesHandToHand, *unarmedFactorsStrengthComboBox);
        loadSettingBool(Settings::game().mAlwaysAllowStealingFromKnockedOutActors, *stealingFromKnockedOutCheckBox);
        loadSettingBool(Settings::navigator().mEnable, *enableNavigatorCheckBox);
        loadSettingInt(Settings::physics().mAsyncNumThreads, *physicsThreadsSpinBox);
        loadSettingBool(
            Settings::game().mAllowActorsToFollowOverWaterSurface, *allowNPCToFollowOverWaterSurfaceCheckBox);
        loadSettingInt(Settings::game().mActorCollisionShapeType, *actorCollisonShapeTypeComboBox);
    }

    // Visuals
    {
        loadSettingBool(Settings::shaders().mAutoUseObjectNormalMaps, *autoUseObjectNormalMapsCheckBox);
        loadSettingBool(Settings::shaders().mAutoUseObjectSpecularMaps, *autoUseObjectSpecularMapsCheckBox);
        loadSettingBool(Settings::shaders().mAutoUseTerrainNormalMaps, *autoUseTerrainNormalMapsCheckBox);
        loadSettingBool(Settings::shaders().mAutoUseTerrainSpecularMaps, *autoUseTerrainSpecularMapsCheckBox);
        loadSettingBool(Settings::shaders().mApplyLightingToEnvironmentMaps, *bumpMapLocalLightingCheckBox);
        loadSettingBool(Settings::shaders().mSoftParticles, *softParticlesCheckBox);
        loadSettingBool(Settings::shaders().mAntialiasAlphaTest, *antialiasAlphaTestCheckBox);
        if (Settings::shaders().mAntialiasAlphaTest == 0)
            antialiasAlphaTestCheckBox->setCheckState(Qt::Unchecked);
        loadSettingBool(Settings::shaders().mAdjustCoverageForAlphaTest, *adjustCoverageForAlphaTestCheckBox);
        loadSettingBool(Settings::shaders().mWeatherParticleOcclusion, *weatherParticleOcclusionCheckBox);
        loadSettingBool(Settings::shaders().mClassicWaterShader, *classicWaterShaderCheckBox);
        loadSettingBool(Settings::game().mUseMagicItemAnimations, *magicItemAnimationsCheckBox);
        connect(animSourcesCheckBox, &QCheckBox::toggled, this, &SettingsPage::slotAnimSourcesToggled);
        loadSettingBool(Settings::game().mUseAdditionalAnimSources, *animSourcesCheckBox);
        if (animSourcesCheckBox->checkState() != Qt::Unchecked)
        {
            loadSettingBool(Settings::game().mWeaponSheathing, *weaponSheathingCheckBox);
            loadSettingBool(Settings::game().mShieldSheathing, *shieldSheathingCheckBox);
        }
        loadSettingBool(Settings::game().mSmoothAnimTransitions, *smoothAnimTransitionsCheckBox);
        loadSettingBool(Settings::game().mTurnToMovementDirection, *turnToMovementDirectionCheckBox);
        loadSettingBool(Settings::game().mSmoothMovement, *smoothMovementCheckBox);
        loadSettingBool(Settings::game().mPlayerMovementIgnoresAnimation, *playerMovementIgnoresAnimationCheckBox);

        connect(distantLandCheckBox, &QCheckBox::toggled, this, &SettingsPage::slotDistantLandToggled);
        bool distantLandEnabled = Settings::terrain().mDistantTerrain && Settings::terrain().mObjectPaging;
        distantLandCheckBox->setCheckState(distantLandEnabled ? Qt::Checked : Qt::Unchecked);
        slotDistantLandToggled(distantLandEnabled);

        loadSettingBool(Settings::terrain().mObjectPagingActiveGrid, *activeGridObjectPagingCheckBox);
        viewingDistanceComboBox->setValue(convertToCells(Settings::camera().mViewingDistance));
        objectPagingMinSizeComboBox->setValue(Settings::terrain().mObjectPagingMinSize);

        loadSettingBool(Settings::game().mDayNightSwitches, *nightDaySwitchesCheckBox);

        connect(postprocessEnabledCheckBox, &QCheckBox::toggled, this, &SettingsPage::slotPostProcessToggled);
        loadSettingBool(Settings::postProcessing().mEnabled, *postprocessEnabledCheckBox);
        loadSettingBool(Settings::postProcessing().mTransparentPostpass, *postprocessTransparentPostpassCheckBox);
        postprocessHDRTimeComboBox->setValue(Settings::postProcessing().mAutoExposureSpeed);

        connect(skyBlendingCheckBox, &QCheckBox::toggled, this, &SettingsPage::slotSkyBlendingToggled);
        loadSettingBool(Settings::fog().mRadialFog, *radialFogCheckBox);
        loadSettingBool(Settings::fog().mExponentialFog, *exponentialFogCheckBox);
        loadSettingBool(Settings::fog().mSkyBlending, *skyBlendingCheckBox);
        skyBlendingStartComboBox->setValue(Settings::fog().mSkyBlendingStart);

        loadSettingBool(Settings::shadows().mActorShadows, *actorShadowsCheckBox);
        loadSettingBool(Settings::shadows().mPlayerShadows, *playerShadowsCheckBox);
        loadSettingBool(Settings::shadows().mTerrainShadows, *terrainShadowsCheckBox);
        loadSettingBool(Settings::shadows().mObjectShadows, *objectShadowsCheckBox);
        loadSettingBool(Settings::shadows().mEnableIndoorShadows, *indoorShadowsCheckBox);

        const auto& boundMethod = Settings::shadows().mComputeSceneBounds.get();
        if (boundMethod == "bounds")
            shadowComputeSceneBoundsComboBox->setCurrentIndex(0);
        else if (boundMethod == "primitives")
            shadowComputeSceneBoundsComboBox->setCurrentIndex(1);
        else
            shadowComputeSceneBoundsComboBox->setCurrentIndex(2);

        const int shadowDistLimit = Settings::shadows().mMaximumShadowMapDistance;
        if (shadowDistLimit > 0)
        {
            shadowDistanceCheckBox->setCheckState(Qt::Checked);
            shadowDistanceSpinBox->setValue(shadowDistLimit);
            shadowDistanceSpinBox->setEnabled(true);
            fadeStartSpinBox->setEnabled(true);
        }

        const float shadowFadeStart = Settings::shadows().mShadowFadeStart;
        if (shadowFadeStart != 0)
            fadeStartSpinBox->setValue(shadowFadeStart);

        const int shadowRes = Settings::shadows().mShadowMapResolution;
        int shadowResIndex = shadowResolutionComboBox->findText(QString::number(shadowRes));
        if (shadowResIndex != -1)
            shadowResolutionComboBox->setCurrentIndex(shadowResIndex);
        else
        {
            shadowResolutionComboBox->addItem(QString::number(shadowRes));
            shadowResolutionComboBox->setCurrentIndex(shadowResolutionComboBox->count() - 1);
        }

        connect(shadowDistanceCheckBox, &QCheckBox::toggled, this, &SettingsPage::slotShadowDistLimitToggled);

        int lightingMethod = 0;
        switch (Settings::shaders().mLightingMethod)
        {
            case SceneUtil::LightingMethod::PerObjectUniform:
                lightingMethod = 0;
                break;
            case SceneUtil::LightingMethod::SingleUBO:
                lightingMethod = 1;
                break;
        }
        lightingMethodComboBox->setCurrentIndex(lightingMethod);
    }

    // Audio
    {
        const std::string& selectedAudioDevice = Settings::sound().mDevice;
        if (selectedAudioDevice.empty() == false)
        {
            int audioDeviceIndex = audioDeviceSelectorComboBox->findData(QString::fromStdString(selectedAudioDevice));
            if (audioDeviceIndex != -1)
            {
                audioDeviceSelectorComboBox->setCurrentIndex(audioDeviceIndex);
            }
        }
        enableHRTFComboBox->setCurrentIndex(toIndex(Settings::sound().mHrtfEnable));
        const std::string& selectedHRTFProfile = Settings::sound().mHrtf;
        if (selectedHRTFProfile.empty() == false)
        {
            int hrtfProfileIndex = hrtfProfileSelectorComboBox->findData(QString::fromStdString(selectedHRTFProfile));
            if (hrtfProfileIndex != -1)
            {
                hrtfProfileSelectorComboBox->setCurrentIndex(hrtfProfileIndex);
            }
        }
        loadSettingBool(Settings::sound().mCameraListener, *cameraListenerCheckBox);
        dopplerSpinBox->setValue(Settings::sound().mDopplerFactor);
    }

    // Interface Changes
    {
        mLastResolutionWidth = Settings::video().mResolutionX;
        mLastResolutionHeight = Settings::video().mResolutionY;
        mLastRecommendedResolutionWidth = mLastResolutionWidth;
        mLastRecommendedResolutionHeight = mLastResolutionHeight;
        loadSettingBool(Settings::game().mShowEffectDuration, *showEffectDurationCheckBox);
        loadSettingBool(Settings::game().mShowEnchantChance, *showEnchantChanceCheckBox);
        loadSettingBool(Settings::game().mShowMeleeInfo, *showMeleeInfoCheckBox);
        loadSettingBool(Settings::game().mShowProjectileDamage, *showProjectileDamageCheckBox);
        loadSettingBool(Settings::gui().mColorTopicEnable, *changeDialogTopicsCheckBox);
        showOwnedComboBox->setCurrentIndex(Settings::game().mShowOwned);
        loadSettingBool(Settings::gui().mStretchMenuBackground, *stretchBackgroundCheckBox);
        loadSettingBool(Settings::gui().mUnskippableIntroVideos, *unskippableIntroVideosCheckBox);
        loadSettingBool(Settings::gui().mLuaHudHideInMenus, *luaAdjustmentsCheckBox);
        connect(controllerMenusCheckBox, &QCheckBox::toggled, this, &SettingsPage::slotControllerMenusToggled);
        loadSettingBool(Settings::gui().mControllerMenus, *controllerMenusCheckBox);
        loadSettingBool(Settings::gui().mControllerTooltips, *controllerMenuTooltipsCheckBox);
        loadSettingBool(Settings::gui().mControllerJoystickDpad, *controllerMenuJoystickDpadCheckBox);
        loadSettingBool(Settings::gui().mSingularContainerTradeWindow, *singularContainerTradeWindowCheckBox);
        loadSettingBool(Settings::gui().mControllerHighlightSelections, *controllerMenuHighlightCheckBox);
        loadSettingBool(Settings::gui().mXboxStyledDialog, *xboxStyledDialogCheckBox);
        loadSettingBool(Settings::gui().mXboxAlchemyUi, *xboxAlchemyUiCheckBox);
        loadSettingBool(Settings::gui().mXboxStyledMinimap, *xboxStyledMinimapCheckBox);
        loadSettingBool(Settings::gui().mXboxStyledFonts, *xboxStyledFontsCheckBox);
        loadSettingBool(Settings::gui().mXboxTabOrder, *xboxTabOrderCheckBox);
        loadSettingBool(Settings::map().mAllowZooming, *useZoomOnMapCheckBox);
        loadSettingBool(Settings::game().mGraphicHerbalism, *graphicHerbalismCheckBox);
        {
            const QSignalBlocker blocker(recommendedScalingDefaultsCheckBox);
            loadSettingBool(Settings::gui().mUseRecommendedScalingDefaults, *recommendedScalingDefaultsCheckBox);
        }
        const double interfaceScaling = Settings::gui().mScalingFactor;
        const double dialogueScaling = Settings::gui().mDialogueInterfaceScaling;
        const double settingsScaling = Settings::gui().mSettingsInterfaceScaling;
        const double effectiveDialogueScaling = dialogueScaling > 0.0 ? dialogueScaling : interfaceScaling;
        const double effectiveSettingsScaling = settingsScaling > 0.0
            ? settingsScaling
            : (Settings::gui().mSettingsWindowIgnoreScaling ? 1.0 : interfaceScaling);
        setScalingValues(interfaceScaling, effectiveDialogueScaling, effectiveSettingsScaling);
        fontSizeSpinBox->setValue(Settings::gui().mFontSize);
        loadSettingFloat(Settings::input().mGamepadCursorSpeed, *gamepadCursorSpeedSpinBox);
        loadSettingBool(Settings::input().mEnableSoftwareMouse, *enableSoftwareMouseCheckBox);
        controllerIconStyleComboBox->setCurrentIndex(
            toIndexControllerIconStyle(Settings::input().mControllerIconStyle));
        loadSettingBool(Settings::input().mControllerWhiteBlackButtonIcons, *controllerWhiteBlackButtonIconsCheckBox);
    }

    // Bug fixes
    {
        loadSettingBool(Settings::game().mPreventMerchantEquipping, *preventMerchantEquippingCheckBox);
        loadSettingBool(
            Settings::game().mTrainersTrainingSkillsBasedOnBaseSkill, *trainersTrainingSkillsBasedOnBaseSkillCheckBox);
    }

    // Miscellaneous
    {
        // Saves
        loadSettingInt(Settings::saves().mMaxQuicksaves, *maximumQuicksavesComboBox);

        // Other Settings
        QString screenshotFormatString = QString::fromStdString(Settings::general().mScreenshotFormat).toUpper();
        if (screenshotFormatComboBox->findText(screenshotFormatString) == -1)
            screenshotFormatComboBox->addItem(screenshotFormatString);
        screenshotFormatComboBox->setCurrentIndex(screenshotFormatComboBox->findText(screenshotFormatString));

        loadSettingBool(Settings::general().mNotifyOnSavedScreenshot, *notifyOnSavedScreenshotCheckBox);

        populateLoadedConfigs();
    }

    // Testing
    {
        loadSettingBool(Settings::input().mGrabCursor, *grabCursorCheckBox);

        bool skipMenu = mGameSettings.value("skip-menu").value.toInt() == 1;
        if (skipMenu)
        {
            skipMenuCheckBox->setCheckState(Qt::Checked);
        }
        startDefaultCharacterAtLabel->setEnabled(skipMenu);
        startDefaultCharacterAtField->setEnabled(skipMenu);

        startDefaultCharacterAtField->setText(mGameSettings.value("start").value);
        runScriptAfterStartupField->setText(mGameSettings.value("script-run").value);
    }
    mLastInterfaceScaling = scalingSpinBox->value();
    return true;
}

void Launcher::SettingsPage::onInterfaceScalingChanged(double value)
{
    const double delta = value - mLastInterfaceScaling;
    if (std::abs(delta) < 0.0001)
        return;

    adjustCustomScaleWithInterfaceDelta(dialogueScalingSpinBox, delta);
    adjustCustomScaleWithInterfaceDelta(settingsInterfaceScalingSpinBox, delta);

    mLastInterfaceScaling = value;
}

void Launcher::SettingsPage::onDialogueScalingChanged(double value)
{
    clampCustomScale(dialogueScalingSpinBox, value);
}

void Launcher::SettingsPage::onSettingsInterfaceScalingChanged(double value)
{
    clampCustomScale(settingsInterfaceScalingSpinBox, value);
}

void Launcher::SettingsPage::setScalingValues(double interfaceScaling, double dialogueScaling, double settingsScaling)
{
    const QSignalBlocker interfaceBlocker(scalingSpinBox);
    const QSignalBlocker dialogueBlocker(dialogueScalingSpinBox);
    const QSignalBlocker settingsBlocker(settingsInterfaceScalingSpinBox);
    scalingSpinBox->setValue(interfaceScaling);
    dialogueScalingSpinBox->setValue(dialogueScaling);
    settingsInterfaceScalingSpinBox->setValue(settingsScaling);
    mLastInterfaceScaling = interfaceScaling;
}

bool Launcher::SettingsPage::applyRecommendedScalingDefaults(int width, int height)
{
    double interfaceScaling = 1.0;
    double dialogueScaling = 1.0;
    double settingsScaling = 1.0;
    if (!tryGetRecommendedScaling(width, height, interfaceScaling, dialogueScaling, settingsScaling))
    {
        return false;
    }

    setScalingValues(interfaceScaling, dialogueScaling, settingsScaling);
    return true;
}

void Launcher::SettingsPage::adjustCustomScaleWithInterfaceDelta(QDoubleSpinBox* spinBox, double delta) const
{
    const double customScale = spinBox->value();
    if (customScale > 0.0)
        spinBox->setValue(std::max(0.5, customScale + delta));
}

void Launcher::SettingsPage::clampCustomScale(QDoubleSpinBox* spinBox, double value)
{
    if (value > 0.0 && value < 0.5)
        spinBox->setValue(0.5);
}

void Launcher::SettingsPage::populateLoadedConfigs()
{
    configsList->clear();

    for (const auto& path : mCfgMgr.getActiveConfigPaths())
    {
        QString configPath = QDir(Files::pathToQString(path)).absolutePath();
        QString toolTipText;

        bool isMainUserConfig = path == mCfgMgr.getUserConfigPath();

        if (path == mCfgMgr.getLocalPath())
        {
            if (isMainUserConfig)
                toolTipText = tr(
                    "Local config directory used because it contains an openmw.cfg.\n"
                    "Logs and settings changed through the launcher and in-game will be saved here.");
            else
                toolTipText = tr("Local config directory used because it contains an openmw.cfg.");
        }
        else if (path == mCfgMgr.getGlobalPath())
        {
            if (isMainUserConfig)
                toolTipText = tr(
                    "Global config directory used because local directory did not contain an openmw.cfg.\n"
                    "Logs and settings changed through the launcher and in-game will be saved here.\n"
                    "This is typically a symptom of a broken OpenMW installation or bad package.");
            else
                toolTipText = tr("Global config directory used because local directory did not contain an openmw.cfg.");
        }
        else
        {
            Config::SettingValue configSetting;
            for (const auto& v : mGameSettings.values(QString("config")))
            {
                if (Files::pathFromQString(v.value) == path)
                {
                    configSetting = v;
                    break;
                }
            }

            if (!configSetting.value.isEmpty())
            {
                const QFileInfo configPathInfo = QFileInfo(configSetting.context + "/openmw.cfg");
                if (isMainUserConfig)
                    toolTipText = tr(
                        "User config directory used because %1 contains the line config=%2.\n"
                        "Logs and settings changed through the launcher and in-game will be saved here.")
                                      .arg(configPathInfo.absoluteFilePath(), configSetting.originalRepresentation);
                else
                    toolTipText = tr("User config directory used because %1 contains the line config=%2.")
                                      .arg(configPathInfo.absoluteFilePath(), configSetting.originalRepresentation);
            }
            else if (isMainUserConfig)
                toolTipText = tr("Logs and settings changed through the launcher and in-game will be saved here.");
        }

        QTreeWidgetItem* configItem = new QTreeWidgetItem(configsList);
        configItem->setText(0, configPath);
        configItem->setToolTip(0, toolTipText);
        configItem->setExpanded(true);

        QUrl directoryUrl = QUrl::fromLocalFile(configPath);
        configItem->setData(0, Role_ThisFile, directoryUrl);
        configItem->setData(0, Role_IsMainUserConfigDirectory, isMainUserConfig);
        configItem->setData(0, Role_ConfigDirectory, directoryUrl);

        for (const auto& fileType : configDirectoryFiles)
        {
            if ((isMainUserConfig || fileType.showInAllConfigDirectories)
                && std::filesystem::exists(path / fileType.name))
            {
                QTreeWidgetItem* fileItem = new QTreeWidgetItem(configItem);
                fileItem->setText(0, fileType.name);

                QUrl url = QUrl::fromLocalFile(Files::pathToQString(path / fileType.name));

                fileItem->setData(0, Role_ThisFile, url);
                fileItem->setData(0, fileType.itemDataRole, url);
                fileItem->setData(0, Role_IsMainUserConfigDirectory, isMainUserConfig);
                fileItem->setData(0, Role_ConfigDirectory, directoryUrl);

                configItem->setData(0, fileType.itemDataRole, url);
            }
        }
    }
}

void Launcher::SettingsPage::saveSettings()
{
    // Game mechanics
    {
        saveSettingBool(*canLootDuringDeathAnimationCheckBox, Settings::game().mCanLootDuringDeathAnimation);
        saveSettingBool(*followersAttackOnSightCheckBox, Settings::game().mFollowersAttackOnSight);
        saveSettingBool(*rebalanceSoulGemValuesCheckBox, Settings::game().mRebalanceSoulGemValues);
        saveSettingBool(*enchantedWeaponsMagicalCheckBox, Settings::game().mEnchantedWeaponsAreMagical);
        saveSettingBool(
            *permanentBarterDispositionChangeCheckBox, Settings::game().mBarterDispositionChangeIsPermanent);
        saveSettingBool(*classicReflectedAbsorbSpellsCheckBox, Settings::game().mClassicReflectedAbsorbSpellsBehavior);
        saveSettingBool(*classicCalmSpellsCheckBox, Settings::game().mClassicCalmSpellsBehavior);
        saveSettingBool(
            *requireAppropriateAmmunitionCheckBox, Settings::game().mOnlyAppropriateAmmunitionBypassesResistance);
        saveSettingBool(*uncappedDamageFatigueCheckBox, Settings::game().mUncappedDamageFatigue);
        saveSettingBool(*normaliseRaceSpeedCheckBox, Settings::game().mNormaliseRaceSpeed);
        saveSettingBool(*swimUpwardCorrectionCheckBox, Settings::game().mSwimUpwardCorrection);
        saveSettingBool(*avoidCollisionsCheckBox, Settings::game().mNPCsAvoidCollisions);
        saveSettingInt(*unarmedFactorsStrengthComboBox, Settings::game().mStrengthInfluencesHandToHand);
        saveSettingBool(*stealingFromKnockedOutCheckBox, Settings::game().mAlwaysAllowStealingFromKnockedOutActors);
        saveSettingBool(*enableNavigatorCheckBox, Settings::navigator().mEnable);
        saveSettingInt(*physicsThreadsSpinBox, Settings::physics().mAsyncNumThreads);
        saveSettingBool(
            *allowNPCToFollowOverWaterSurfaceCheckBox, Settings::game().mAllowActorsToFollowOverWaterSurface);
        saveSettingInt(*actorCollisonShapeTypeComboBox, Settings::game().mActorCollisionShapeType);
    }

    // Visuals
    {
        saveSettingBool(*autoUseObjectNormalMapsCheckBox, Settings::shaders().mAutoUseObjectNormalMaps);
        saveSettingBool(*autoUseObjectSpecularMapsCheckBox, Settings::shaders().mAutoUseObjectSpecularMaps);
        saveSettingBool(*autoUseTerrainNormalMapsCheckBox, Settings::shaders().mAutoUseTerrainNormalMaps);
        saveSettingBool(*autoUseTerrainSpecularMapsCheckBox, Settings::shaders().mAutoUseTerrainSpecularMaps);
        saveSettingBool(*bumpMapLocalLightingCheckBox, Settings::shaders().mApplyLightingToEnvironmentMaps);
        saveSettingBool(*radialFogCheckBox, Settings::fog().mRadialFog);
        saveSettingBool(*softParticlesCheckBox, Settings::shaders().mSoftParticles);
        saveSettingBool(*antialiasAlphaTestCheckBox, Settings::shaders().mAntialiasAlphaTest);
        saveSettingBool(*adjustCoverageForAlphaTestCheckBox, Settings::shaders().mAdjustCoverageForAlphaTest);
        saveSettingBool(*weatherParticleOcclusionCheckBox, Settings::shaders().mWeatherParticleOcclusion);
        saveSettingBool(*classicWaterShaderCheckBox, Settings::shaders().mClassicWaterShader);
        saveSettingBool(*magicItemAnimationsCheckBox, Settings::game().mUseMagicItemAnimations);
        saveSettingBool(*animSourcesCheckBox, Settings::game().mUseAdditionalAnimSources);
        saveSettingBool(*weaponSheathingCheckBox, Settings::game().mWeaponSheathing);
        saveSettingBool(*shieldSheathingCheckBox, Settings::game().mShieldSheathing);
        saveSettingBool(*turnToMovementDirectionCheckBox, Settings::game().mTurnToMovementDirection);
        saveSettingBool(*smoothAnimTransitionsCheckBox, Settings::game().mSmoothAnimTransitions);
        saveSettingBool(*smoothMovementCheckBox, Settings::game().mSmoothMovement);
        saveSettingBool(*playerMovementIgnoresAnimationCheckBox, Settings::game().mPlayerMovementIgnoresAnimation);

        const bool wantDistantLand = distantLandCheckBox->checkState() == Qt::Checked;
        if (wantDistantLand != (Settings::terrain().mDistantTerrain && Settings::terrain().mObjectPaging))
        {
            Settings::terrain().mDistantTerrain.set(wantDistantLand);
            Settings::terrain().mObjectPaging.set(wantDistantLand);
        }

        saveSettingBool(*activeGridObjectPagingCheckBox, Settings::terrain().mObjectPagingActiveGrid);
        Settings::camera().mViewingDistance.set(convertToUnits(viewingDistanceComboBox->value()));
        Settings::terrain().mObjectPagingMinSize.set(objectPagingMinSizeComboBox->value());
        saveSettingBool(*nightDaySwitchesCheckBox, Settings::game().mDayNightSwitches);
        saveSettingBool(*postprocessEnabledCheckBox, Settings::postProcessing().mEnabled);
        saveSettingBool(*postprocessTransparentPostpassCheckBox, Settings::postProcessing().mTransparentPostpass);
        Settings::postProcessing().mAutoExposureSpeed.set(postprocessHDRTimeComboBox->value());
        saveSettingBool(*radialFogCheckBox, Settings::fog().mRadialFog);
        saveSettingBool(*exponentialFogCheckBox, Settings::fog().mExponentialFog);
        saveSettingBool(*skyBlendingCheckBox, Settings::fog().mSkyBlending);
        Settings::fog().mSkyBlendingStart.set(skyBlendingStartComboBox->value());

        static constexpr std::array<SceneUtil::LightingMethod, 2> lightingMethodMap = {
            SceneUtil::LightingMethod::PerObjectUniform,
            SceneUtil::LightingMethod::SingleUBO,
        };
        Settings::shaders().mLightingMethod.set(lightingMethodMap[lightingMethodComboBox->currentIndex()]);

        const int cShadowDist
            = shadowDistanceCheckBox->checkState() != Qt::Unchecked ? shadowDistanceSpinBox->value() : 0;
        Settings::shadows().mMaximumShadowMapDistance.set(cShadowDist);
        const float cFadeStart = fadeStartSpinBox->value();
        if (cShadowDist > 0)
            Settings::shadows().mShadowFadeStart.set(cFadeStart);

        const bool cActorShadows = actorShadowsCheckBox->checkState() != Qt::Unchecked;
        const bool cObjectShadows = objectShadowsCheckBox->checkState() != Qt::Unchecked;
        const bool cTerrainShadows = terrainShadowsCheckBox->checkState() != Qt::Unchecked;
        const bool cPlayerShadows = playerShadowsCheckBox->checkState() != Qt::Unchecked;
        if (cActorShadows || cObjectShadows || cTerrainShadows || cPlayerShadows)
        {
            Settings::shadows().mEnableShadows.set(true);
            Settings::shadows().mActorShadows.set(cActorShadows);
            Settings::shadows().mPlayerShadows.set(cPlayerShadows);
            Settings::shadows().mObjectShadows.set(cObjectShadows);
            Settings::shadows().mTerrainShadows.set(cTerrainShadows);
        }
        else
        {
            Settings::shadows().mEnableShadows.set(false);
            Settings::shadows().mActorShadows.set(false);
            Settings::shadows().mPlayerShadows.set(false);
            Settings::shadows().mObjectShadows.set(false);
            Settings::shadows().mTerrainShadows.set(false);
        }

        Settings::shadows().mEnableIndoorShadows.set(indoorShadowsCheckBox->checkState() != Qt::Unchecked);
        Settings::shadows().mShadowMapResolution.set(shadowResolutionComboBox->currentText().toInt());

        auto index = shadowComputeSceneBoundsComboBox->currentIndex();
        if (index == 0)
            Settings::shadows().mComputeSceneBounds.set("bounds");
        else if (index == 1)
            Settings::shadows().mComputeSceneBounds.set("primitives");
        else
            Settings::shadows().mComputeSceneBounds.set("none");
    }

    // Audio
    {
        if (audioDeviceSelectorComboBox->currentIndex() != 0)
            Settings::sound().mDevice.set(audioDeviceSelectorComboBox->currentText().toStdString());
        else
            Settings::sound().mDevice.set({});

        static constexpr std::array<Settings::HrtfMode, 3> hrtfModes{
            Settings::HrtfMode::Auto,
            Settings::HrtfMode::Disable,
            Settings::HrtfMode::Enable,
        };
        Settings::sound().mHrtfEnable.set(hrtfModes[enableHRTFComboBox->currentIndex()]);

        if (hrtfProfileSelectorComboBox->currentIndex() != 0)
            Settings::sound().mHrtf.set(hrtfProfileSelectorComboBox->currentText().toStdString());
        else
            Settings::sound().mHrtf.set({});

        const bool cCameraListener = cameraListenerCheckBox->checkState() != Qt::Unchecked;
        Settings::sound().mCameraListener.set(cCameraListener);

        Settings::sound().mDopplerFactor.set(dopplerSpinBox->value());
    }

    // Interface Changes
    {
        saveSettingBool(*showEffectDurationCheckBox, Settings::game().mShowEffectDuration);
        saveSettingBool(*showEnchantChanceCheckBox, Settings::game().mShowEnchantChance);
        saveSettingBool(*showMeleeInfoCheckBox, Settings::game().mShowMeleeInfo);
        saveSettingBool(*showProjectileDamageCheckBox, Settings::game().mShowProjectileDamage);
        saveSettingBool(*changeDialogTopicsCheckBox, Settings::gui().mColorTopicEnable);
        saveSettingInt(*showOwnedComboBox, Settings::game().mShowOwned);
        saveSettingBool(*stretchBackgroundCheckBox, Settings::gui().mStretchMenuBackground);
        saveSettingBool(*unskippableIntroVideosCheckBox, Settings::gui().mUnskippableIntroVideos);
        saveSettingBool(*luaAdjustmentsCheckBox, Settings::gui().mLuaHudHideInMenus);
        saveSettingBool(*controllerMenusCheckBox, Settings::gui().mControllerMenus);
        saveSettingBool(*controllerMenuTooltipsCheckBox, Settings::gui().mControllerTooltips);
        saveSettingBool(*controllerMenuJoystickDpadCheckBox, Settings::gui().mControllerJoystickDpad);
        saveSettingBool(*singularContainerTradeWindowCheckBox, Settings::gui().mSingularContainerTradeWindow);
        saveSettingBool(*controllerMenuHighlightCheckBox, Settings::gui().mControllerHighlightSelections);
        saveSettingBool(*xboxStyledDialogCheckBox, Settings::gui().mXboxStyledDialog);
        saveSettingBool(*xboxAlchemyUiCheckBox, Settings::gui().mXboxAlchemyUi);
        saveSettingBool(*xboxStyledMinimapCheckBox, Settings::gui().mXboxStyledMinimap);
        saveSettingBool(*xboxStyledFontsCheckBox, Settings::gui().mXboxStyledFonts);
        saveSettingBool(*xboxTabOrderCheckBox, Settings::gui().mXboxTabOrder);
        saveSettingBool(*useZoomOnMapCheckBox, Settings::map().mAllowZooming);
        saveSettingBool(*graphicHerbalismCheckBox, Settings::game().mGraphicHerbalism);
        saveSettingBool(*recommendedScalingDefaultsCheckBox, Settings::gui().mUseRecommendedScalingDefaults);
        Settings::gui().mScalingFactor.set(scalingSpinBox->value());
        Settings::gui().mDialogueInterfaceScaling.set(dialogueScalingSpinBox->value());
        Settings::gui().mSettingsInterfaceScaling.set(settingsInterfaceScalingSpinBox->value());
        Settings::gui().mFontSize.set(fontSizeSpinBox->value());
        saveSettingFloat(*gamepadCursorSpeedSpinBox, Settings::input().mGamepadCursorSpeed);
        saveSettingBool(*enableSoftwareMouseCheckBox, Settings::input().mEnableSoftwareMouse);
        Settings::input().mControllerIconStyle.set(
            fromIndexControllerIconStyle(controllerIconStyleComboBox->currentIndex()));
        saveSettingBool(*controllerWhiteBlackButtonIconsCheckBox, Settings::input().mControllerWhiteBlackButtonIcons);
    }

    // Bug fixes
    {
        saveSettingBool(*preventMerchantEquippingCheckBox, Settings::game().mPreventMerchantEquipping);
        saveSettingBool(
            *trainersTrainingSkillsBasedOnBaseSkillCheckBox, Settings::game().mTrainersTrainingSkillsBasedOnBaseSkill);
    }

    // Miscellaneous
    {
        // Saves Settings
        saveSettingInt(*maximumQuicksavesComboBox, Settings::saves().mMaxQuicksaves);

        // Other Settings
        Settings::general().mScreenshotFormat.set(screenshotFormatComboBox->currentText().toLower().toStdString());
        saveSettingBool(*notifyOnSavedScreenshotCheckBox, Settings::general().mNotifyOnSavedScreenshot);
    }

    // Testing
    {
        saveSettingBool(*grabCursorCheckBox, Settings::input().mGrabCursor);

        int skipMenu = skipMenuCheckBox->checkState() == Qt::Checked;
        if (skipMenu != mGameSettings.value("skip-menu").value.toInt())
            mGameSettings.setValue("skip-menu", { QString::number(skipMenu) });

        QString startCell = startDefaultCharacterAtField->text();
        if (startCell != mGameSettings.value("start").value)
        {
            mGameSettings.setValue("start", { startCell });
        }
        QString scriptRun = runScriptAfterStartupField->text();
        if (scriptRun != mGameSettings.value("script-run").value)
            mGameSettings.setValue("script-run", { scriptRun });
    }
}

void Launcher::SettingsPage::slotLoadedCellsChanged(QStringList cellNames)
{
    loadCellsForAutocomplete(std::move(cellNames));
}

void Launcher::SettingsPage::slotAnimSourcesToggled(bool checked)
{
    weaponSheathingCheckBox->setEnabled(checked);
    shieldSheathingCheckBox->setEnabled(checked);
    if (!checked)
    {
        weaponSheathingCheckBox->setCheckState(Qt::Unchecked);
        shieldSheathingCheckBox->setCheckState(Qt::Unchecked);
    }
}

void Launcher::SettingsPage::slotControllerMenusToggled(bool checked)
{
    controllerMenuTooltipsCheckBox->setEnabled(checked);
    controllerMenuJoystickDpadCheckBox->setEnabled(checked);
    singularContainerTradeWindowCheckBox->setEnabled(checked);
    controllerMenuHighlightCheckBox->setEnabled(checked);
    controllerIconStyleLabel->setEnabled(checked);
    controllerIconStyleComboBox->setEnabled(checked);
    controllerWhiteBlackButtonIconsCheckBox->setEnabled(checked);
}

void Launcher::SettingsPage::slotPostProcessToggled(bool checked)
{
    postprocessTransparentPostpassCheckBox->setEnabled(checked);
    postprocessHDRTimeComboBox->setEnabled(checked);
    postprocessHDRTimeLabel->setEnabled(checked);
}

void Launcher::SettingsPage::slotSkyBlendingToggled(bool checked)
{
    skyBlendingStartComboBox->setEnabled(checked);
    skyBlendingStartLabel->setEnabled(checked);
}

void Launcher::SettingsPage::slotShadowDistLimitToggled(bool checked)
{
    shadowDistanceSpinBox->setEnabled(checked);
    fadeStartSpinBox->setEnabled(checked);
}

void Launcher::SettingsPage::slotDistantLandToggled(bool checked)
{
    activeGridObjectPagingCheckBox->setEnabled(checked);
    objectPagingMinSizeComboBox->setEnabled(checked);
}

void Launcher::SettingsPage::slotOpenFile(QTreeWidgetItem* item)
{
    QUrl configFolderUrl = item->data(0, Role_ThisFile).toUrl();
    QDesktopServices::openUrl(configFolderUrl);
}
