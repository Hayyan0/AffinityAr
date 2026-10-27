// Tauri IPC helper
const invoke = async (cmd, args = {}) => {
  try {
    if (window.__TAURI__ && window.__TAURI__.core && typeof window.__TAURI__.core.invoke === 'function') {
      return await window.__TAURI__.core.invoke(cmd, args);
    }
    if (window.__TAURI_INTERNALS__ && typeof window.__TAURI_INTERNALS__.invoke === 'function') {
      return await window.__TAURI_INTERNALS__.invoke(cmd, args);
    }
    if (window.__TAURI__ && typeof window.__TAURI__.invoke === 'function') {
      return await window.__TAURI__.invoke(cmd, args);
    }
  } catch (err) {
    console.error(`Error invoking command '${cmd}':`, err);
    throw err;
  }
  console.warn(`[No Tauri IPC available for command: ${cmd}]`, args);
  return null;
};

const state = {
  currentStep: 1,
  detectedInstallations: [],
  selectedApp: {
    name: "Affinity",
    path: "",
    exe_path: "",
    version: "",
    install_type: "",
    badge: "",
    description: "",
    detected: false
  },
  systemInfo: {
    free_disk: "20+ GB",
    payload_size_kb: 546
  },
  installMode: "embedded",
  isTransitioning: false,
  pendingStep: null,
  isInstalling: false,
  launcherPath: "",
  isUpdateMode: false,
  existingVersion: ""
};

function hasValidTarget() {
  return !!(state.selectedApp && state.selectedApp.detected &&
            state.selectedApp.path && state.selectedApp.exe_path);
}

const dom = {
  splash: document.getElementById('splash-screen'),
  actionbar: document.querySelector('.custom-actionbar'),
  btnMinimize: document.getElementById('btn-window-minimize'),
  btnClose: document.getElementById('btn-window-close'),

  stepperItems: document.querySelectorAll('.stepper-item'),

  // Step 1
  s1SingleCard: document.getElementById('s1-single-app-card'),
  s1MultiSection: document.getElementById('s1-multi-version-section'),
  s1VersionCardsList: document.getElementById('s1-version-cards-list'),
  s1Name: document.getElementById('s1-detected-name'),
  s1Sub: document.getElementById('s1-detected-sub'),
  s1Badge: document.getElementById('s1-detected-badge'),
  btnToStep2: document.getElementById('btn-to-step2'),

  // Step 2
  s2SummaryCard: document.getElementById('s2-summary-card'),
  s2Name: document.getElementById('s2-detected-name'),
  s2Sub: document.getElementById('s2-detected-sub'),
  s2DetectedBadgeText: document.getElementById('s2-detected-badge-text'),
  versionSelectSection: document.getElementById('version-select-section'),
  versionCardsList: document.getElementById('version-cards-list'),
  existingInstallCard: document.getElementById('existing-install-card'),
  existingInstallDetails: document.getElementById('existing-install-details'),
  btnUninstallExisting: document.getElementById('btn-uninstall-existing'),
  targetPathField: document.getElementById('target-path-field'),
  btnBrowse: document.getElementById('btn-browse-folder'),
  pathValidationTxt: document.getElementById('txt-path-validation'),
  storageInfoTxt: document.getElementById('txt-storage-info'),
  btnToStep3: document.getElementById('btn-to-step3'),
  btnBackStep1: document.getElementById('btn-back-step1'),

  // Step 3
  cardLauncher: document.getElementById('mode-card-launcher'),
  cardEmbedded: document.getElementById('mode-card-embedded'),
  modeLauncherDesc: document.getElementById('mode-card-launcher-desc'),
  modeLauncherCheck1: document.getElementById('mode-card-launcher-check1'),
  modeLauncherCheck2: document.getElementById('mode-card-launcher-check2'),
  modeEmbeddedDesc: document.getElementById('mode-card-embedded-desc'),
  modeEmbeddedCheck1: document.getElementById('mode-card-embedded-check1'),
  modeEmbeddedCheck2: document.getElementById('mode-card-embedded-check2'),
  btnStartInstall: document.getElementById('btn-start-install'),
  btnBackStep2: document.getElementById('btn-back-step2'),

  // Step 4
  s4Title: document.getElementById('s4-title'),
  progressContainer: document.getElementById('progress-container'),
  progressPercent: document.getElementById('progress-percent'),
  progressTaskName: document.getElementById('progress-task-name'),
  progressSizeText: document.getElementById('progress-size-text'),
  progressFill: document.getElementById('progress-fill'),
  installTimer: document.getElementById('install-timer'),
  s4TargetSpec: document.getElementById('s4-target-spec'),
  s4ModeLabel: document.getElementById('s4-mode-label'),
  s4TargetPath: document.getElementById('s4-target-path'),
  tasks: [
    document.getElementById('task-1'),
    document.getElementById('task-2'),
    document.getElementById('task-3'),
    document.getElementById('task-4')
  ],
  installErrorBox: document.getElementById('install-error-box'),
  installErrorMsg: document.getElementById('install-error-msg'),
  btnRetryInstall: document.getElementById('btn-retry-install'),
  btnErrorBack: document.getElementById('btn-error-back'),

  // Step 5
  s5HeroTitle: document.getElementById('s5-hero-title'),
  s5StatusTag: document.getElementById('s5-status-tag'),
  s5FinalApp: document.getElementById('s5-final-app'),
  s5FinalMode: document.getElementById('s5-final-mode'),
  s5FinalPath: document.getElementById('s5-final-path'),
  btnLaunchApp: document.getElementById('btn-launch-app'),
  btnOpenSupport: document.getElementById('btn-open-support'),
  btnAuthorTwitter: document.getElementById('btn-author-twitter'),
  btnFinishClose: document.getElementById('btn-finish-close'),

  sidebarFooterText: document.getElementById('sidebar-footer-text')
};

window.addEventListener('DOMContentLoaded', async () => {
  initWindowControls();
  initStepEvents();
  initModeSelection();
  updateDetectedUI();
  updateTargetValidity();

  await Promise.all([
    loadSystemInfo(),
    loadInstallerVersion(),
    detectInstalledAffinity()
  ]);

  setTimeout(() => {
    if (dom.splash) {
      dom.splash.classList.add('fade-out');
      setTimeout(() => {
        dom.splash.style.display = 'none';
      }, 500);
    }
  }, 1200);
});

function initWindowControls() {
  if (dom.actionbar) {
    dom.actionbar.addEventListener('mousedown', async (e) => {
      if (e.button === 0 && !e.target.closest('.control-btn')) {
        try {
          await invoke('start_dragging');
        } catch (err) {
          console.warn('Native drag error:', err);
        }
      }
    });
  }

  if (dom.btnMinimize) {
    dom.btnMinimize.addEventListener('click', async (e) => {
      e.preventDefault();
      e.stopPropagation();
      try {
        await invoke('minimize_window');
      } catch (err) {
        console.error('Minimize error:', err);
      }
    });
  }

  if (dom.btnClose) {
    dom.btnClose.addEventListener('click', async (e) => {
      e.preventDefault();
      e.stopPropagation();
      if (state.isInstalling) {
        const confirmed = window.confirm("التثبيت قيد التنفيذ. هل تريد إغلاق المُثبت؟ قد تترك بعض الملفات غير مكتملة.");
        if (!confirmed) return;
      }
      try {
        await invoke('close_window');
      } catch (err) {
        console.error('Close error:', err);
      }
    });
  }
}

async function loadSystemInfo() {
  try {
    const info = await invoke('get_system_info', { targetPath: state.selectedApp.path || null });
    if (info) {
      state.systemInfo = info;
      renderStorageInfo();
    }
  } catch (err) {
    console.warn("Failed to query system info:", err);
  }
}

async function loadInstallerVersion() {
  try {
    let version = "";
    if (window.__TAURI__ && window.__TAURI__.app && typeof window.__TAURI__.app.getVersion === 'function') {
      version = await window.__TAURI__.app.getVersion();
    }
    if (!version && window.__TAURI_INTERNALS__ && typeof window.__TAURI_INTERNALS__.invoke === 'function') {
      version = await window.__TAURI_INTERNALS__.invoke('plugin:app|version');
    }
    if (version && dom.sidebarFooterText) {
      dom.sidebarFooterText.textContent = `AFFINITY ARABIC • v${version}`;
    }
  } catch (err) {
    console.warn("Failed to read installer version:", err);
  }
}

async function detectInstalledAffinity() {
  try {
    const list = await invoke('detect_installations');
    if (Array.isArray(list) && list.length > 0) {
      state.detectedInstallations = list;
      const found = list.find(a => a.install_type === "enterprise") || list[0];
      state.selectedApp = { ...found };
    } else {
      state.detectedInstallations = [];
      state.selectedApp = {
        name: "Affinity",
        path: "",
        exe_path: "",
        version: "",
        install_type: "",
        badge: "NOT FOUND",
        description: "",
        detected: false
      };
    }
  } catch (err) {
    console.warn("Detection warning:", err);
  }

  renderVersionSelector();
  updateDetectedUI();
  updateTargetValidity();
  updateModeCardsForApp();
  await checkExistingInstallation();
}

function renderVersionSelector() {
  const list = state.detectedInstallations;
  const isMulti = (Array.isArray(list) && list.length > 1);

  // Step 1: Multi Section vs Single Card
  if (dom.s1MultiSection && dom.s1SingleCard) {
    if (isMulti) {
      dom.s1MultiSection.style.display = 'block';
      dom.s1SingleCard.style.display = 'none';
      renderCardsIntoContainer(dom.s1VersionCardsList, list);
    } else {
      dom.s1MultiSection.style.display = 'none';
      dom.s1SingleCard.style.display = 'flex';
    }
  }

  // Step 2: Multi Section vs Summary Card
  if (dom.versionSelectSection && dom.versionCardsList) {
    if (isMulti) {
      dom.versionSelectSection.style.display = 'block';
      if (dom.s2SummaryCard) dom.s2SummaryCard.style.display = 'none';
      renderCardsIntoContainer(dom.versionCardsList, list);
    } else {
      dom.versionSelectSection.style.display = 'none';
      if (dom.s2SummaryCard) dom.s2SummaryCard.style.display = 'flex';
    }
  }
}

function renderCardsIntoContainer(container, list) {
  if (!container) return;
  container.replaceChildren();

  list.forEach((app) => {
    const isSelected = (state.selectedApp.path && state.selectedApp.path.toLowerCase() === app.path.toLowerCase());
    const card = document.createElement('div');
    card.className = `version-option-card ${isSelected ? 'selected' : ''}`;
    card.dataset.path = app.path || '';
    card.setAttribute('role', 'button');
    card.setAttribute('tabindex', '0');

    const right = document.createElement('div');
    right.className = 'version-option-right';
    const radio = document.createElement('div');
    radio.className = 'version-radio-circle';
    const texts = document.createElement('div');
    texts.className = 'version-option-texts';
    const title = document.createElement('span');
    title.className = 'version-option-title';
    title.textContent = app.name;
    const desc = document.createElement('span');
    desc.className = 'version-option-desc';
    desc.textContent = app.description || app.path;
    texts.append(title, desc);
    right.append(radio, texts);

    const badge = document.createElement('span');
    badge.className = 'version-option-badge';
    badge.textContent = app.badge || (app.install_type === 'msix' ? 'STORE / MSIX' : 'ENTERPRISE');

    card.append(right, badge);

    const activate = async () => { await selectTargetApp(app); };
    card.addEventListener('click', activate);
    card.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        activate();
      }
    });

    container.appendChild(card);
  });
}

async function selectTargetApp(app) {
  state.selectedApp = { ...app };

  document.querySelectorAll('.version-option-card').forEach((c) => {
    const selectedPath = state.selectedApp.path ? state.selectedApp.path.toLowerCase() : '';
    if (c.dataset.path && c.dataset.path.toLowerCase() === selectedPath) {
      c.classList.add('selected');
    } else {
      c.classList.remove('selected');
    }
  });

  updateDetectedUI();
  updateTargetValidity();
  updateModeCardsForApp();
  await loadSystemInfo();
  await checkExistingInstallation();
}

function updateModeCardsForApp() {
  const isMsix = (state.selectedApp.install_type === "msix");

  if (dom.modeLauncherDesc) {
    dom.modeLauncherDesc.textContent = isMsix
      ? "تشغيل نسخة المتجر (MSIX) عبر مشغّل مستقل يقوم بحقن التعريب تلقائياً دون لمس حزمة المتجر الأصلية."
      : "تشغيل البرنامج عبر اختصار مستقل AffinityArabic.exe دون استبدال ملفات Affinity الأصلية.";
  }
  if (dom.modeLauncherCheck1) {
    dom.modeLauncherCheck1.textContent = isMsix
      ? "إنشاء اختصار مباشر على سطح المكتب (Affinity Arabic)"
      : "إنشاء اختصار مباشر على سطح المكتب";
  }
  if (dom.modeLauncherCheck2) {
    dom.modeLauncherCheck2.textContent = isMsix
      ? "يعمل بتوافق كامل مع تحديثات المتجر دون كسر التوقيع الرقمي"
      : "لا يستبدل أي ملف أصلي في مجلد Affinity";
  }

  if (dom.modeEmbeddedDesc) {
    dom.modeEmbeddedDesc.textContent = isMsix
      ? "دمج التعريب داخل حزمة MSIX وتسجيلها بحيث يعمل تلقائياً عند فتح Affinity من قائمة ابدأ أو شريط المهام."
      : "تفعيل التعريب مباشرة عند فتح Affinity من أي اختصار أو مستند وبشكل سلس وتلقائي.";
  }
  if (dom.modeEmbeddedCheck1) {
    dom.modeEmbeddedCheck1.textContent = isMsix
      ? "يعمل تلقائياً مع قائمة ابدأ وشريط المهام والملفات المفتوحة"
      : "يعمل تلقائيًا مع كافة اختصارات وملفات البرنامج";
  }
  if (dom.modeEmbeddedCheck2) {
    dom.modeEmbeddedCheck2.textContent = isMsix
      ? "تسجيل الحزمة المدمجة في ويندوز مع الحفاظ على كافة الوظائف"
      : "حفظ نسخة احتياطية للملفات الأصلية تلقائيًا";
  }
}

async function checkExistingInstallation() {
  if (!state.selectedApp.path) {
    state.isUpdateMode = false;
    state.existingVersion = "";
    if (dom.existingInstallCard) dom.existingInstallCard.style.display = 'none';
    return;
  }
  try {
    const result = await invoke('check_existing_install', { targetPath: state.selectedApp.path });
    const installed = !!(result && result.installed);

    state.isUpdateMode = installed;
    state.existingVersion = installed ? (result.version || "") : "";

    if (dom.existingInstallCard) {
      if (installed) {
        const modeLabels = [];
        if (result.seamless) modeLabels.push("دمج تلقائي");
        if (result.launcher) modeLabels.push("مشغّل مستقل");
        const versionLabel = state.existingVersion
          ? `الإصدار ${state.existingVersion}`
          : "إصدار سابق";
        const modeLabel = modeLabels.length ? modeLabels.join(" • ") : "";
        if (dom.existingInstallDetails) {
          dom.existingInstallDetails.textContent = modeLabel
            ? `${versionLabel} • ${modeLabel}`
            : versionLabel;
          dom.existingInstallDetails.style.color = "";
        }
        resetUninstallUI();
        dom.existingInstallCard.style.display = 'flex';
      } else {
        dom.existingInstallCard.style.display = 'none';
      }
    }

    const startSpan = dom.btnStartInstall ? dom.btnStartInstall.querySelector('span') : null;
    if (startSpan) {
      startSpan.textContent = installed ? "تحديث التعريب" : "تثبيت التعريب";
    }
  } catch (err) {
    console.warn("Existing install check failed:", err);
  }
}

function resetUninstallUI() {
  if (dom.btnUninstallExisting) {
    dom.btnUninstallExisting.disabled = false;
    dom.btnUninstallExisting.style.display = '';
    const label = dom.btnUninstallExisting.querySelector('span');
    if (label) label.textContent = "إزالة التعريب";
  }
  if (dom.existingInstallDetails) dom.existingInstallDetails.style.color = "";
}

async function uninstallExisting() {
  if (!dom.btnUninstallExisting || state.isInstalling) return;
  const confirmed = window.confirm("هل تريد إزالة تعريب Affinity واسترجاع الملفات الأصلية؟");
  if (!confirmed) return;
  state.isInstalling = true;
  dom.btnUninstallExisting.disabled = true;
  const label = dom.btnUninstallExisting.querySelector('span');
  if (label) label.textContent = "جاري الإزالة...";

  try {
    const response = await invoke('uninstall_package', { targetPath: state.selectedApp.path });
    if (dom.existingInstallDetails) {
      dom.existingInstallDetails.textContent = (response && response.message)
        ? response.message
        : "تمت إزالة التعريب بنجاح.";
      dom.existingInstallDetails.style.color = "#A7F175";
    }
    if (dom.btnUninstallExisting) dom.btnUninstallExisting.style.display = 'none';
    state.isUpdateMode = false;
    state.existingVersion = "";

    const startSpan = dom.btnStartInstall ? dom.btnStartInstall.querySelector('span') : null;
    if (startSpan) startSpan.textContent = "تثبيت التعريب";
  } catch (err) {
    if (dom.existingInstallDetails) {
      dom.existingInstallDetails.textContent = String(err);
      dom.existingInstallDetails.style.color = "#FF5C5C";
    }
  } finally {
    state.isInstalling = false;
    if (dom.btnUninstallExisting) {
      dom.btnUninstallExisting.disabled = false;
      const currentLabel = dom.btnUninstallExisting.querySelector('span');
      if (currentLabel) currentLabel.textContent = "إزالة التعريب";
    }
  }
}

function getFormattedPayloadSize() {
  const kb = state.systemInfo.payload_size_kb || 546;
  if (kb >= 1024) {
    return (kb / 1024).toFixed(1) + " MB";
  }
  return kb + " KB";
}

function renderStorageInfo() {
  if (dom.storageInfoTxt) {
    const payloadStr = getFormattedPayloadSize();
    dom.storageInfoTxt.textContent = `${payloadStr} مطلوب • ${state.systemInfo.free_disk} متوفر`;
  }
}

function updateDetectedUI() {
  const app = state.selectedApp;
  const valid = hasValidTarget();

  if (valid) {
    if (dom.s1Name) dom.s1Name.textContent = app.name;
    if (dom.s1Sub) dom.s1Sub.textContent = `الإصدار ${app.version}`;
    if (dom.s1Badge) dom.s1Badge.textContent = app.badge || "AFFINITY";

    if (dom.s2Name) dom.s2Name.textContent = app.name;
    if (dom.s2Sub) dom.s2Sub.textContent = `الإصدار ${app.version} • متوافق`;
    if (dom.s2DetectedBadgeText) dom.s2DetectedBadgeText.textContent = app.badge || "عُثر عليه";
    if (dom.targetPathField) dom.targetPathField.value = app.path;
    if (dom.pathValidationTxt) {
      dom.pathValidationTxt.textContent = "";
      dom.pathValidationTxt.parentElement.className = "";
    }
  } else {
    if (dom.s1Name) dom.s1Name.textContent = "لم يتم العثور على Affinity";
    if (dom.s1Sub) dom.s1Sub.textContent = "ثبّت Affinity أو حدّد مجلد التثبيت يدوياً من الخطوة التالية.";
    if (dom.s1Badge) dom.s1Badge.textContent = "NOT FOUND";

    if (dom.s2Name) dom.s2Name.textContent = "لم يتم العثور على Affinity";
    if (dom.s2Sub) dom.s2Sub.textContent = "استخدم زر التصفح لتحديد مجلد يحتوي على Affinity.exe";
    if (dom.s2DetectedBadgeText) dom.s2DetectedBadgeText.textContent = "NOT FOUND";
    if (dom.targetPathField) dom.targetPathField.value = app.path || "";
  }

  renderStorageInfo();

  if (dom.s4TargetSpec) {
    dom.s4TargetSpec.textContent = valid
      ? `${app.name.toUpperCase()} (v${app.version})`
      : "بانتظار تحديد مجلد Affinity";
  }
  if (dom.s4TargetPath) dom.s4TargetPath.textContent = app.path || "";

  if (dom.s5FinalApp) {
    dom.s5FinalApp.textContent = valid ? `${app.name} (${app.version})` : "";
  }
  if (dom.s5FinalPath) dom.s5FinalPath.textContent = app.path || "";
}

function updateTargetValidity() {
  const valid = hasValidTarget();
  if (dom.btnToStep3) dom.btnToStep3.disabled = !valid;
  if (dom.btnStartInstall) {
    dom.btnStartInstall.disabled = !valid || state.isInstalling;
  }
  return valid;
}

function navigateToStep(newStep) {
  // A navigation requested mid-animation must be queued, not dropped: the
  // install-complete handler navigates to step 5 while step 4 may still be
  // animating in, and an early return here would leave the UI stuck forever.
  if (state.isTransitioning) {
    state.pendingStep = newStep;
    return;
  }

  if (newStep === state.currentStep) return;

  const currentStepEl = document.getElementById(`step-0${state.currentStep}`);
  const nextStepEl = document.getElementById(`step-0${newStep}`);

  if (!currentStepEl || !nextStepEl) {
    state.isTransitioning = false;
    return;
  }

  state.isTransitioning = true;
  currentStepEl.classList.remove('page-enter');
  currentStepEl.classList.add('page-exit');

  setTimeout(() => {
    currentStepEl.classList.remove('active', 'page-exit');

    nextStepEl.classList.add('active', 'page-enter');

    state.currentStep = newStep;
    updateStepperUI();

    setTimeout(() => {
      nextStepEl.classList.remove('page-enter');
      state.isTransitioning = false;

      if (state.pendingStep !== null) {
        const queuedStep = state.pendingStep;
        state.pendingStep = null;
        navigateToStep(queuedStep);
      }
    }, 350);
  }, 220);
}

function updateStepperUI() {
  dom.stepperItems.forEach(item => {
    const stepNum = parseInt(item.getAttribute('data-step'), 10);
    const statusText = item.querySelector('.stepper-status');

    item.classList.remove('active', 'completed');

    if (stepNum < state.currentStep) {
      item.classList.add('completed');
      if (statusText) statusText.textContent = '';
    } else if (stepNum === state.currentStep) {
      item.classList.add('active');
      if (statusText) statusText.textContent = 'الخطوة الحالية';
    } else {
      if (statusText) statusText.textContent = '';
    }
  });
}

function initStepEvents() {
  if (dom.btnToStep2) {
    dom.btnToStep2.addEventListener('click', () => navigateToStep(2));
  }

  if (dom.btnToStep3) {
    dom.btnToStep3.addEventListener('click', () => navigateToStep(3));
  }

  if (dom.btnBackStep1) {
    dom.btnBackStep1.addEventListener('click', () => navigateToStep(1));
  }

  if (dom.btnBrowse) {
    dom.btnBrowse.addEventListener('click', async () => {
      try {
        const picked = await invoke('browse_folder');
        if (picked) {
          dom.targetPathField.value = picked;
          const validation = await invoke('validate_folder', { folderPath: picked });
          if (validation && validation.detected) {
            const existingIdx = state.detectedInstallations.findIndex(a => a.path.toLowerCase() === validation.path.toLowerCase());
            if (existingIdx === -1) {
              state.detectedInstallations.push(validation);
            } else {
              state.detectedInstallations[existingIdx] = validation;
            }
            renderVersionSelector();
            await selectTargetApp(validation);
            if (dom.pathValidationTxt) {
              dom.pathValidationTxt.textContent = `المجلد صالح: ${validation.name}`;
              dom.pathValidationTxt.parentElement.className = "status-valid";
            }
          } else {
            state.selectedApp = {
              name: "Affinity",
              path: picked,
              exe_path: "",
              version: "",
              install_type: "",
              badge: "NOT FOUND",
              description: "",
              detected: false
            };
            updateDetectedUI();
            updateTargetValidity();
            if (dom.pathValidationTxt) {
              dom.pathValidationTxt.textContent = "تحذير: لم يتم العثور على ملف Affinity.exe في هذا المجلد. التعريب يدعم نسخة Affinity الموحدة فقط.";
              dom.pathValidationTxt.parentElement.className = "status-invalid";
            }
          }
          await loadSystemInfo();
          await checkExistingInstallation();
        }
      } catch (e) {
        console.error('Browse folder error:', e);
      }
    });
  }

  if (dom.btnBackStep2) {
    dom.btnBackStep2.addEventListener('click', () => navigateToStep(2));
  }

  if (dom.btnUninstallExisting) {
    dom.btnUninstallExisting.addEventListener('click', uninstallExisting);
  }

  if (dom.btnStartInstall) {
    dom.btnStartInstall.addEventListener('click', () => {
      if (state.isInstalling || !updateTargetValidity()) return;
      navigateToStep(4);
      runInstallationProcess();
    });
  }

  if (dom.btnRetryInstall) {
    dom.btnRetryInstall.addEventListener('click', () => {
      if (state.isInstalling) return;
      runInstallationProcess();
    });
  }

  if (dom.btnErrorBack) {
    dom.btnErrorBack.addEventListener('click', () => {
      state.isInstalling = false;
      navigateToStep(3);
    });
  }

  if (dom.btnLaunchApp) {
    dom.btnLaunchApp.addEventListener('click', async () => {
      const useLauncher = state.installMode === "launcher" && state.launcherPath;
      const exeToLaunch = useLauncher ? state.launcherPath : state.selectedApp.exe_path;
      const launchType = useLauncher ? "launcher" : state.selectedApp.install_type;
      if (!exeToLaunch) {
        window.alert("تعذر تحديد مسار تشغيل Affinity.");
        return;
      }
      try {
        await invoke('launch_affinity', {
          exePath: exeToLaunch,
          installType: launchType
        });
        setTimeout(() => invoke('close_window'), 800);
      } catch (err) {
        console.error("Launch error:", err);
        window.alert(`تعذر تشغيل Affinity:\n${String(err)}`);
      }
    });
  }

  const handleTwitterClick = async () => {
    try {
      await invoke('open_twitter_url');
    } catch (e) {
      window.open('https://x.com/HayyanCodes', '_blank');
    }
  };

  if (dom.btnAuthorTwitter) {
    dom.btnAuthorTwitter.addEventListener('click', handleTwitterClick);
  }

  if (dom.btnOpenSupport) {
    dom.btnOpenSupport.addEventListener('click', async () => {
      try {
        await invoke('open_support_url');
      } catch (e) {
        window.open('https://buymeacoffee.com/hayyan', '_blank');
      }
    });
  }

  if (dom.btnFinishClose) {
    dom.btnFinishClose.addEventListener('click', async () => {
      try {
        await invoke('close_window');
      } catch (e) {}
    });
  }

  dom.stepperItems.forEach(item => {
    item.setAttribute('role', 'button');
    item.setAttribute('tabindex', '0');
    const goBack = () => {
      const stepNum = parseInt(item.getAttribute('data-step'), 10);
      if (stepNum < state.currentStep && state.currentStep !== 4 && !state.isInstalling) {
        navigateToStep(stepNum);
      }
    };
    item.addEventListener('click', goBack);
    item.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        goBack();
      }
    });
  });
}

function selectInstallMode(mode) {
  state.installMode = mode;
  const launcherSelected = mode === "launcher";
  const cards = [
    [dom.cardLauncher, launcherSelected],
    [dom.cardEmbedded, !launcherSelected]
  ];
  cards.forEach(([card, selected]) => {
    if (!card) return;
    card.classList.toggle('selected', selected);
    card.setAttribute('aria-pressed', selected ? 'true' : 'false');
    const radio = card.querySelector('.mode-radio-circle');
    const icon = card.querySelector('.mode-icon-box');
    if (radio) radio.classList.toggle('active', selected);
    if (icon) icon.classList.toggle('active', selected);
  });
}

function initModeSelection() {
  if (!dom.cardLauncher || !dom.cardEmbedded) return;
  [dom.cardLauncher, dom.cardEmbedded].forEach((card) => {
    const mode = card.dataset.mode;
    card.setAttribute('role', 'button');
    card.setAttribute('tabindex', '0');
    card.addEventListener('click', () => selectInstallMode(mode));
    card.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        selectInstallMode(mode);
      }
    });
  });
  selectInstallMode(state.installMode);
}

async function runInstallationProcess() {
  if (state.isInstalling) return;
  if (!state.selectedApp.path || !state.selectedApp.exe_path) {
    return;
  }
  if (!updateTargetValidity()) return;
  state.isInstalling = true;
  const updating = state.isUpdateMode;

  if (dom.s4Title) dom.s4Title.textContent = updating ? "جاري التحديث" : "جاري التثبيت";
  const s4Sub = document.getElementById('step-04-sub');
  if (s4Sub) s4Sub.textContent = updating
    ? "يتم الآن تحديث ملفات المحرك العربي إلى أحدث إصدار."
    : "يتم الآن نسخ ملفات المحرك العربي وإعداد بيئة العمل.";

  if (dom.installErrorBox) dom.installErrorBox.style.display = 'none';
  if (dom.progressContainer) dom.progressContainer.style.display = 'flex';

  if (dom.s4ModeLabel) {
    dom.s4ModeLabel.textContent = state.installMode === "embedded" ? "دمج تلقائي مع التطبيق" : "مشغّل مستقل";
  }

  const payloadStr = getFormattedPayloadSize();

  // The copy is a single Rust call; show an indeterminate bar rather than
  // pretending that timed phases have completed before they actually have.
  setProgress(0, "جارٍ نسخ الملفات وإعداد التعريب...", payloadStr);
  if (dom.progressFill) dom.progressFill.classList.add('indeterminate');
  setTaskState(0, 'running');
  setTaskState(1, 'pending');
  setTaskState(2, 'pending');
  setTaskState(3, 'pending');

  if (dom.installTimer) {
    dom.installTimer.textContent = "جاري تنفيذ عملية التثبيت...";
  }

  const installOptions = {
    target_path: state.selectedApp.path,
    install_seamless_dll: state.installMode === 'embedded',
    install_launcher: state.installMode === 'launcher',
    create_desktop_shortcut: true,
    install_type: state.selectedApp.install_type || 'enterprise'
  };

  try {
    const installResponse = await invoke('install_package', { options: installOptions });
    if (!installResponse || !installResponse.success) {
      throw new Error(installResponse ? installResponse.message : "فشلت عملية التثبيت لأسباب غير معروفة.");
    }

    if (installResponse.launcher_path) {
      state.launcherPath = installResponse.launcher_path;
    }

    if (dom.progressFill) dom.progressFill.classList.remove('indeterminate');
    setProgress(100, "اكتمل التثبيت بنجاح!", payloadStr);
    setTaskState(0, 'done');
    setTaskState(1, 'done');
    setTaskState(2, 'done');
    setTaskState(3, 'done');

    if (dom.installTimer) {
      dom.installTimer.textContent = "اكتملت العملية بنجاح.";
    }

    if (dom.s5HeroTitle) dom.s5HeroTitle.textContent = updating ? "تم التحديث بنجاح" : "تم التثبيت بنجاح";
    if (dom.s5StatusTag) {
      dom.s5StatusTag.textContent = updating ? "أفينتي محدّث وجاهز لإبداعك." : "أفينتي جاهز لإبداعك.";
    }
    if (dom.s5FinalMode) {
      dom.s5FinalMode.textContent = state.installMode === "embedded"
        ? "دمج تلقائي مع التطبيق • نسخة احتياطية محفوظة"
        : "مشغّل مستقل • اختصار مخصص على سطح المكتب";
    }

    state.isInstalling = false;

    setTimeout(() => {
      navigateToStep(5);
    }, 500);

  } catch (err) {
    state.isInstalling = false;
    if (dom.progressFill) dom.progressFill.classList.remove('indeterminate');
    console.error("Installation failed:", err);

    markActiveTaskError();

    if (dom.installErrorBox && dom.installErrorMsg) {
      dom.installErrorMsg.textContent = String(err);
      dom.installErrorBox.style.display = 'flex';
    }

    if (dom.installTimer) {
      dom.installTimer.textContent = "توقف التثبيت بسبب خطأ.";
    }
    updateTargetValidity();
  }
}

function setProgress(percent, taskName, sizeText) {
  if (dom.progressPercent) dom.progressPercent.textContent = percent;
  if (dom.progressFill) dom.progressFill.style.width = `${percent}%`;
  if (dom.progressTaskName) dom.progressTaskName.textContent = taskName;
  if (dom.progressSizeText) dom.progressSizeText.textContent = sizeText;
}

function setTaskState(index, status) {
  const row = dom.tasks[index];
  if (!row) return;

  const iconCircle = row.querySelector('.task-icon-circle');
  const tag = row.querySelector('.task-status-tag');

  if (!iconCircle || !tag) return;

  iconCircle.className = `task-icon-circle ${status}`;
  tag.className = `task-status-tag ${status}`;

  if (status === 'done') {
    iconCircle.innerHTML = `
      <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="#A7F175" stroke-width="3" stroke-linecap="round" stroke-linejoin="round">
        <polyline points="20 6 9 17 4 12"/>
      </svg>
    `;
    tag.textContent = 'مكتمل';
  } else if (status === 'running') {
    iconCircle.innerHTML = `<div class="spinner-dot"></div>`;
    tag.textContent = 'جارِ التنفيذ';
  } else if (status === 'error') {
    iconCircle.innerHTML = `
      <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="#FF5C5C" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
        <line x1="18" y1="6" x2="6" y2="18"/>
        <line x1="6" y1="6" x2="18" y2="18"/>
      </svg>
    `;
    tag.textContent = 'فشل';
  } else {
    iconCircle.innerHTML = '';
    tag.textContent = 'في الانتظار';
  }
}

function markActiveTaskError() {
  dom.tasks.forEach((row, i) => {
    const iconCircle = row.querySelector('.task-icon-circle');
    if (iconCircle && iconCircle.classList.contains('running')) {
      setTaskState(i, 'error');
    }
  });
}
