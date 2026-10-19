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
  selectedApp: {
    name: "Affinity",
    path: "C:\\Program Files\\Affinity\\Affinity",
    exe_path: "C:\\Program Files\\Affinity\\Affinity\\Affinity.exe",
    version: "3.2.x"
  },
  systemInfo: {
    free_disk: "20+ GB",
    payload_size_kb: 546
  },
  installMode: "embedded",
  isTransitioning: false,
  isInstalling: false,
  launcherPath: "",
  isUpdateMode: false, 
  existingVersion: ""
};

const dom = {
  splash: document.getElementById('splash-screen'),
  actionbar: document.querySelector('.custom-actionbar'),
  btnMinimize: document.getElementById('btn-window-minimize'),
  btnClose: document.getElementById('btn-window-close'),

  stepperItems: document.querySelectorAll('.stepper-item'),

  // Step 1
  s1Name: document.getElementById('s1-detected-name'),
  s1Sub: document.getElementById('s1-detected-sub'),
  btnToStep2: document.getElementById('btn-to-step2'),

  // Step 2
  s2Name: document.getElementById('s2-detected-name'),
  s2Sub: document.getElementById('s2-detected-sub'),
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
  btnOpenTwitter: document.getElementById('btn-open-twitter'),
  btnAuthorTwitter: document.getElementById('btn-author-twitter'),
  btnFinishClose: document.getElementById('btn-finish-close')
};

window.addEventListener('DOMContentLoaded', async () => {
  initWindowControls();
  initStepEvents();
  initModeSelection();

  await Promise.all([
    loadSystemInfo(),
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
    const info = await invoke('get_system_info', { targetPath: state.selectedApp.path });
    if (info) {
      state.systemInfo = info;
      renderStorageInfo();
    }
  } catch (err) {
    console.warn("Failed to query system info:", err);
  }
}

async function detectInstalledAffinity() {
  try {
    const list = await invoke('detect_installations');
    if (Array.isArray(list) && list.length > 0) {
      const found = list.find(a => a.path.toLowerCase().includes("affinity\\affinity")) || list[0];
      state.selectedApp = {
        name: found.name,
        path: found.path,
        exe_path: found.exe_path,
        version: found.version || "3.2.x"
      };
    }
  } catch (err) {
    console.warn("Detection warning:", err);
  }

  updateDetectedUI();
  await checkExistingInstallation();
}

async function checkExistingInstallation() {
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
    if (dom.btnUninstallExisting) dom.btnUninstallExisting.disabled = false;
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

  if (dom.s1Name) dom.s1Name.textContent = app.name;
  if (dom.s1Sub) dom.s1Sub.textContent = `الإصدار ${app.version}`;

  if (dom.s2Name) dom.s2Name.textContent = app.name;
  if (dom.s2Sub) dom.s2Sub.textContent = `الإصدار ${app.version} • متوافق`;
  if (dom.targetPathField) dom.targetPathField.value = app.path;

  renderStorageInfo();

  if (dom.s4TargetSpec) dom.s4TargetSpec.textContent = `${app.name.toUpperCase()} (v${app.version})`;
  if (dom.s4TargetPath) dom.s4TargetPath.textContent = app.path;

  if (dom.s5FinalApp) dom.s5FinalApp.textContent = `${app.name} (${app.version})`;
  if (dom.s5FinalPath) dom.s5FinalPath.textContent = app.path;
}

function navigateToStep(newStep) {
  if (state.isTransitioning || newStep === state.currentStep) return;
  state.isTransitioning = true;

  const currentStepEl = document.getElementById(`step-0${state.currentStep}`);
  const nextStepEl = document.getElementById(`step-0${newStep}`);

  if (!currentStepEl || !nextStepEl) {
    state.isTransitioning = false;
    return;
  }

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
            state.selectedApp.path = picked;
            state.selectedApp.name = validation.name;
            state.selectedApp.exe_path = validation.exe_path;
            state.selectedApp.version = validation.version;
            updateDetectedUI();
            if (dom.btnToStep3) dom.btnToStep3.disabled = false;
            if (dom.pathValidationTxt) {
              dom.pathValidationTxt.textContent = `المجلد صالح ويحتوي على ${validation.name} (Affinity.exe)`;
              dom.pathValidationTxt.parentElement.className = "status-valid";
            }
          } else {
            state.selectedApp.path = picked;
            if (dom.btnToStep3) dom.btnToStep3.disabled = true;
            if (dom.pathValidationTxt) {
              if (validation && validation.name && validation.name.includes("غير مدعوم")) {
                dom.pathValidationTxt.textContent = "تنبيه: هذا التعريب مخصص للنسخة الموحدة (Affinity.exe) ولا يدعم الإصدارات القديمة";
              } else {
                dom.pathValidationTxt.textContent = "تحذير: لم يتم العثور على ملف Affinity.exe في هذا المجلد";
              }
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
      navigateToStep(4);
      runInstallationProcess();
    });
  }

  if (dom.btnRetryInstall) {
    dom.btnRetryInstall.addEventListener('click', () => {
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
      try {
        const exeToLaunch = (state.installMode === "launcher" && state.launcherPath)
          ? state.launcherPath
          : state.selectedApp.exe_path;
        await invoke('launch_affinity', { exePath: exeToLaunch });
        setTimeout(() => invoke('close_window'), 800);
      } catch (err) {
        console.error("Launch error:", err);
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
    item.addEventListener('click', () => {
      const stepNum = parseInt(item.getAttribute('data-step'), 10);
      if (stepNum < state.currentStep && state.currentStep !== 4 && !state.isInstalling) {
        navigateToStep(stepNum);
      }
    });
  });
}

function initModeSelection() {
  if (dom.cardLauncher && dom.cardEmbedded) {
    dom.cardLauncher.addEventListener('click', () => {
      state.installMode = "launcher";
      dom.cardLauncher.classList.add('selected');
      dom.cardLauncher.querySelector('.mode-radio-circle').classList.add('active');
      dom.cardLauncher.querySelector('.mode-icon-box').classList.add('active');

      dom.cardEmbedded.classList.remove('selected');
      dom.cardEmbedded.querySelector('.mode-radio-circle').classList.remove('active');
      dom.cardEmbedded.querySelector('.mode-icon-box').classList.remove('active');
    });

    dom.cardEmbedded.addEventListener('click', () => {
      state.installMode = "embedded";
      dom.cardEmbedded.classList.add('selected');
      dom.cardEmbedded.querySelector('.mode-radio-circle').classList.add('active');
      dom.cardEmbedded.querySelector('.mode-icon-box').classList.add('active');

      dom.cardLauncher.classList.remove('selected');
      dom.cardLauncher.querySelector('.mode-radio-circle').classList.remove('active');
      dom.cardLauncher.querySelector('.mode-icon-box').classList.remove('active');
    });
  }
}

async function runInstallationProcess() {
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
  const totalKb = state.systemInfo.payload_size_kb || 546;

  setProgress(0, "بدء تهيئة الملفات...", `0 KB / ${payloadStr}`);
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
    create_desktop_shortcut: true
  };

  try {
    await delay(250);
    setProgress(25, "فحص مجلد Affinity وصلاحيات النظام...", `${Math.round(totalKb * 0.25)} KB / ${payloadStr}`);
    setTaskState(0, 'done');
    setTaskState(1, 'running');

    await delay(300);
    setProgress(50, "إنشاء نسخة احتياطية للملفات الأصلية...", `${Math.round(totalKb * 0.5)} KB / ${payloadStr}`);
    setTaskState(1, 'done');
    setTaskState(2, 'running');

    const installResponse = await invoke('install_package', { options: installOptions });
    if (!installResponse || !installResponse.success) {
      throw new Error(installResponse ? installResponse.message : "فشلت عملية التثبيت لأسباب غير معروفة.");
    }

    if (installResponse.launcher_path) {
      state.launcherPath = installResponse.launcher_path;
    }

    setProgress(80, "تثبيت مكتبة التشكيل العربي ومحرك النصوص...", `${Math.round(totalKb * 0.8)} KB / ${payloadStr}`);
    setTaskState(2, 'done');
    setTaskState(3, 'running');

    await delay(350);
    setProgress(100, "اكتمل التثبيت بنجاح!", `${totalKb} KB / ${payloadStr}`);
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
    console.error("Installation failed:", err);

    markActiveTaskError();

    if (dom.installErrorBox && dom.installErrorMsg) {
      dom.installErrorMsg.textContent = String(err);
      dom.installErrorBox.style.display = 'flex';
    }

    if (dom.installTimer) {
      dom.installTimer.textContent = "توقف التثبيت بسبب خطأ.";
    }
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

function delay(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}
