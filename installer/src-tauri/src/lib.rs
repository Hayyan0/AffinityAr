use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;
use std::process::Command;

// Runtime binaries
const AFFINITY_ARABIC_DLL: &[u8] = include_bytes!("../../../bin/affinity_arabic.dll");
const LIBSTACKTRACE_DLL: &[u8] = include_bytes!("../../../bin/libstacktrace.dll");
const AFFINITY_ARABIC_EXE: &[u8] = include_bytes!("../../../bin/AffinityArabic.exe");
const INSTALLER_VERSION: &str = "1.0.0";

#[link(name = "version")]
extern "system" {
    fn GetFileVersionInfoSizeW(lptstrFilename: *const u16, lpdwHandle: *mut u32) -> u32;
    fn GetFileVersionInfoW(lptstrFilename: *const u16, dwHandle: u32, dwLen: u32, lpData: *mut u8) -> i32;
    fn VerQueryValueW(pBlock: *const u8, lpSubBlock: *const u16, lplpBuffer: *mut *mut u8, puLen: *mut u32) -> i32;
}

#[link(name = "kernel32")]
extern "system" {
    fn GetDiskFreeSpaceExW(
        lpDirectoryName: *const u16,
        lpFreeBytesAvailableToCaller: *mut u64,
        lpTotalNumberOfBytes: *mut u64,
        lpTotalNumberOfFreeBytes: *mut u64,
    ) -> i32;
}

#[derive(Serialize)]
pub struct SystemInfo {
    pub free_disk: String,
    pub payload_size_kb: usize,
}

#[derive(Serialize)]
pub struct AffinityInstallation {
    pub name: String,
    pub path: String,
    pub exe_path: String,
    pub version: String,
    pub detected: bool,
}

#[derive(Deserialize)]
pub struct InstallOptions {
    pub target_path: String,
    pub install_seamless_dll: bool,
    pub install_launcher: bool,
    pub create_desktop_shortcut: bool,
}

#[derive(Serialize)]
pub struct ExistingInstall {
    pub installed: bool,
    pub version: String,
    pub seamless: bool,
    pub launcher: bool,
}

#[derive(Serialize)]
pub struct UninstallResponse {
    pub success: bool,
    pub message: String,
}

#[derive(Serialize)]
pub struct InstallResponse {
    pub success: bool,
    pub message: String,
    pub launcher_path: Option<String>,
}

fn is_access_denied(e: &std::io::Error) -> bool {
    e.kind() == std::io::ErrorKind::PermissionDenied || e.raw_os_error() == Some(5)
}

fn admin_required_message() -> String {
    "تعذّر تنفيذ العملية لعدم توفر صلاحيات كافية. انقر بزر الماوس الأيمن على المُثبت واختر «تشغيل كمسؤول» ثم أعد المحاولة.".to_string()
}

fn map_fs_err(e: std::io::Error, context: &str) -> String {
    if is_access_denied(&e) {
        admin_required_message()
    } else {
        format!("{}: {}", context, e)
    }
}

fn query_real_file_version(exe_path: &str) -> String {
    use std::os::windows::ffi::OsStrExt;
    let wide_path: Vec<u16> = std::ffi::OsStr::new(exe_path).encode_wide().chain(std::iter::once(0)).collect();
    unsafe {
        let size = GetFileVersionInfoSizeW(wide_path.as_ptr(), std::ptr::null_mut());
        if size > 0 {
            let mut data = vec![0u8; size as usize];
            if GetFileVersionInfoW(wide_path.as_ptr(), 0, size, data.as_mut_ptr()) != 0 {
                let mut buf_ptr: *mut u8 = std::ptr::null_mut();
                let mut len: u32 = 0;
                let sub_block: Vec<u16> = std::ffi::OsStr::new("\\").encode_wide().chain(std::iter::once(0)).collect();
                if VerQueryValueW(data.as_ptr(), sub_block.as_ptr(), &mut buf_ptr, &mut len) != 0 && len >= 52 {
                    let p32 = buf_ptr as *const u32;
                    let ms = *p32.offset(2);
                    let ls = *p32.offset(3);
                    return format!("{}.{}.{}.{}", ms >> 16, ms & 0xffff, ls >> 16, ls & 0xffff);
                }
            }
        }
    }
    "3.2.x".to_string()
}

fn query_real_free_disk_space(dir_path: &str) -> String {
    use std::os::windows::ffi::OsStrExt;
    let wide_dir: Vec<u16> = std::ffi::OsStr::new(dir_path).encode_wide().chain(std::iter::once(0)).collect();
    unsafe {
        let mut free_bytes: u64 = 0;
        if GetDiskFreeSpaceExW(wide_dir.as_ptr(), &mut free_bytes, std::ptr::null_mut(), std::ptr::null_mut()) != 0 {
            let gb = (free_bytes as f64) / (1024.0 * 1024.0 * 1024.0);
            return format!("{:.1} GB", gb);
        }
    }
    "20+ GB".to_string()
}

#[tauri::command]
fn get_system_info(target_path: Option<String>) -> SystemInfo {
    let check_path = target_path.unwrap_or_else(|| r"C:\Program Files\Affinity\Affinity".to_string());
    let payload_kb = (AFFINITY_ARABIC_DLL.len() + LIBSTACKTRACE_DLL.len() + AFFINITY_ARABIC_EXE.len()) / 1024;

    SystemInfo {
        free_disk: query_real_free_disk_space(&check_path),
        payload_size_kb: payload_kb,
    }
}

#[tauri::command]
fn detect_installations() -> Vec<AffinityInstallation> {
    let mut results = Vec::new();

    let candidates = vec![
        ("Affinity", r"C:\Program Files\Affinity\Affinity", "Affinity.exe"),
        ("Affinity (D:)", r"D:\Program Files\Affinity\Affinity", "Affinity.exe"),
        ("Affinity (E:)", r"E:\Program Files\Affinity\Affinity", "Affinity.exe"),
    ];

    for (name, dir, exe) in candidates {
        let exe_full = Path::new(dir).join(exe);
        if exe_full.exists() {
            let exe_str = exe_full.to_string_lossy().to_string();
            let ver = query_real_file_version(&exe_str);
            results.push(AffinityInstallation {
                name: name.to_string(),
                path: dir.to_string(),
                exe_path: exe_str,
                version: ver,
                detected: true,
            });
        }
    }

    if results.is_empty() {
        if let Ok(local_app_data) = std::env::var("LOCALAPPDATA") {
            let user_dir = format!(r"{}\Programs\Affinity\Affinity", local_app_data);
            let exe_full = Path::new(&user_dir).join("Affinity.exe");
            if exe_full.exists() {
                let exe_str = exe_full.to_string_lossy().to_string();
                let ver = query_real_file_version(&exe_str);
                results.push(AffinityInstallation {
                    name: "Affinity".to_string(),
                    path: user_dir,
                    exe_path: exe_str,
                    version: ver,
                    detected: true,
                });
            }
        }
    }

    results
}

#[tauri::command]
fn browse_folder() -> Option<String> {
    let folder = rfd::FileDialog::new()
        .set_title("Select Affinity Installation Folder")
        .pick_folder();

    folder.map(|p| p.to_string_lossy().to_string())
}

#[tauri::command]
fn validate_folder(folder_path: String) -> AffinityInstallation {
    let path = Path::new(&folder_path);
    let target_exe = path.join("Affinity.exe");

    if target_exe.exists() {
        let exe_str = target_exe.to_string_lossy().to_string();
        let ver = query_real_file_version(&exe_str);
        return AffinityInstallation {
            name: "Affinity".to_string(),
            path: folder_path,
            exe_path: exe_str,
            version: ver,
            detected: true,
        };
    }

    for legacy in ["Designer.exe", "Photo.exe", "Publisher.exe"] {
        if path.join(legacy).exists() {
            return AffinityInstallation {
                name: "Affinity Legacy (غير مدعوم)".to_string(),
                path: folder_path,
                exe_path: "".to_string(),
                version: "".to_string(),
                detected: false,
            };
        }
    }

    AffinityInstallation {
        name: "Custom Directory".to_string(),
        path: folder_path,
        exe_path: "".to_string(),
        version: "".to_string(),
        detected: false,
    }
}

#[tauri::command]
fn check_existing_install(target_path: String) -> ExistingInstall {
    let target = Path::new(&target_path);
    let core_dll = target.join("arabic_support").join("bin").join("affinity_arabic.dll");
    let root_dll = target.join("affinity_arabic.dll");
    let version_marker = target.join("arabic_support").join("version.txt");

    let installed = core_dll.exists() || root_dll.exists();

    if !installed {
        return ExistingInstall {
            installed: false,
            version: String::new(),
            seamless: false,
            launcher: false,
        };
    }

    let mut version = String::new();
    if let Ok(content) = fs::read_to_string(&version_marker) {
        version = content.trim().to_string();
    }

    ExistingInstall {
        installed: true,
        version,
        seamless: target.join("libstacktrace_orig.dll").exists(),
        launcher: target.join("AffinityArabic.exe").exists(),
    }
}
// Keep Dev
fn is_dev_project_tree(support_dir: &Path) -> bool {
    support_dir.join("src").is_dir()
        || support_dir.join("installer").is_dir()
        || support_dir.join(".git").is_dir()
        || support_dir.join("docs").is_dir()
        || support_dir.join("tests").is_dir()
        || support_dir.join("scripts").is_dir()
        || support_dir.join("resources").is_dir()
        || support_dir.join("Cargo.toml").is_file()
        || support_dir.join("LICENSE").is_file()
        || support_dir.join("README.md").is_file()
}

#[tauri::command]
fn uninstall_package(target_path: String) -> Result<UninstallResponse, String> {
    let target = Path::new(&target_path);
    if !target.exists() {
        return Err(format!("Selected folder does not exist: {}", target_path));
    }

    let orig = target.join("libstacktrace_orig.dll");
    let seamless_current = target.join("libstacktrace.dll");
    if orig.exists() {
        fs::copy(&orig, &seamless_current)
            .map_err(|e| map_fs_err(e, "تعذر استرجاع الملف الأصلي (تأكد من إغلاق Affinity ثم أعد المحاولة)"))?;
        let _ = fs::remove_file(&orig);
    }

    let root_dll = target.join("affinity_arabic.dll");
    if root_dll.exists() {
        fs::remove_file(&root_dll)
            .map_err(|e| map_fs_err(e, "تعذر حذف affinity_arabic.dll"))?;
    }

    let root_launcher = target.join("AffinityArabic.exe");
    if root_launcher.exists() {
        fs::remove_file(&root_launcher)
            .map_err(|e| map_fs_err(e, "تعذر حذف AffinityArabic.exe"))?;
    }

    let _ = remove_desktop_shortcut("Affinity (Arabic)");

    let mut note = String::new();
    let support_dir = target.join("arabic_support");
    if support_dir.exists() {
        if is_dev_project_tree(&support_dir) {
            note = " ملاحظة: مجلد arabic_support يحتوي على ملفات مصدر المشروع وتم الإبقاء عليه كما هو.".to_string();
        } else {
            let bin_dir = support_dir.join("bin");
            for file in ["affinity_arabic.dll", "AffinityArabic.exe", "libstacktrace.dll", "libstacktrace_orig.dll"] {
                let p = bin_dir.join(file);
                if p.exists() {
                    fs::remove_file(&p).map_err(|e| map_fs_err(e, "تعذر حذف ملفات الدعم العربي"))?;
                }
            }
            let version_marker = support_dir.join("version.txt");
            if version_marker.exists() {
                let _ = fs::remove_file(&version_marker);
            }
            let _ = fs::remove_file(support_dir.join("hook.log"));
            let backup_dir = support_dir.join("backup");
            if backup_dir.exists() {
                fs::remove_dir_all(&backup_dir)
                    .map_err(|e| map_fs_err(e, "تعذر حذف مجلد النسخ الاحتياطي"))?;
            }
            if bin_dir.exists() {
                fs::remove_dir(&bin_dir).map_err(|e| map_fs_err(e, "تعذر حذف مجلد bin"))?;
            }
            fs::remove_dir(&support_dir).map_err(|e| map_fs_err(e, "تعذر حذف مجلد الدعم العربي"))?;
        }
    } else {
        let legacy_backup = target.join("backup").join("libstacktrace.dll.bak");
        let _ = fs::remove_file(&legacy_backup);
        let _ = fs::remove_dir(target.join("backup"));
    }

    Ok(UninstallResponse {
        success: true,
        message: format!("تمت إزالة التعريب واسترجاع ملفات Affinity الأصلية بنجاح.{}", note),
    })
}

fn remove_desktop_shortcut(shortcut_name: &str) -> Result<(), String> {
    let script = format!(
        r#"$shortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) '{}.lnk'; if (Test-Path $shortcut) {{ Remove-Item -LiteralPath $shortcut -Force }}"#,
        shortcut_name
    );
    Command::new("powershell")
        .args(["-NoProfile", "-NonInteractive", "-Command", &script])
        .output()
        .map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
fn install_package(options: InstallOptions) -> Result<InstallResponse, String> {
    let target_dir = Path::new(&options.target_path);
    if !target_dir.exists() {
        return Err(format!("Selected folder does not exist: {}", options.target_path));
    }

    let arabic_support_dir = target_dir.join("arabic_support");
    let bin_dir = arabic_support_dir.join("bin");
    let backup_dir = arabic_support_dir.join("backup");

    fs::create_dir_all(&bin_dir)
        .map_err(|e| map_fs_err(e, &format!("Failed to create directory {}", bin_dir.display())))?;
    fs::create_dir_all(&backup_dir)
        .map_err(|e| map_fs_err(e, "Failed to create backup directory"))?;

    let version_marker = arabic_support_dir.join("version.txt");
    let _ = fs::write(&version_marker, INSTALLER_VERSION);

    let core_dll_path = bin_dir.join("affinity_arabic.dll");
    fs::write(&core_dll_path, AFFINITY_ARABIC_DLL)
        .map_err(|e| map_fs_err(e, "Failed to write affinity_arabic.dll"))?;

    let root_dll_path = target_dir.join("affinity_arabic.dll");
    fs::write(&root_dll_path, AFFINITY_ARABIC_DLL)
        .map_err(|e| map_fs_err(e, "Failed to write affinity_arabic.dll"))?;

    let version_dll_path = target_dir.join("version.dll");
    if version_dll_path.exists() {
        if let Ok(meta) = fs::metadata(&version_dll_path) {
            if meta.len() > 240000 && meta.len() < 265000 {
                let _ = fs::remove_file(&version_dll_path);
            }
        }
    }

    let _ = fs::remove_file(arabic_support_dir.join("hook.log"));
    let _ = fs::remove_file(target_dir.join("hook.log"));

    if options.install_seamless_dll {
        let orig_libstacktrace = target_dir.join("libstacktrace_orig.dll");
        let target_libstacktrace = target_dir.join("libstacktrace.dll");
        let backup_libstacktrace = backup_dir.join("libstacktrace.dll.bak");

        if !orig_libstacktrace.exists() && target_libstacktrace.exists() {
            fs::copy(&target_libstacktrace, &orig_libstacktrace)
                .map_err(|e| map_fs_err(e, "Failed to create libstacktrace_orig.dll"))?;
        }

        if target_libstacktrace.exists() && !backup_libstacktrace.exists() {
            let _ = fs::copy(&target_libstacktrace, &backup_libstacktrace);
        }

        fs::write(&target_libstacktrace, LIBSTACKTRACE_DLL)
            .map_err(|e| map_fs_err(e, "Failed to write libstacktrace.dll"))?;
    }

    let mut launcher_path_out = None;
    if options.install_launcher {
        let launcher_in_bin = bin_dir.join("AffinityArabic.exe");
        let _ = fs::write(&launcher_in_bin, AFFINITY_ARABIC_EXE);

        let launcher_in_root = target_dir.join("AffinityArabic.exe");
        fs::write(&launcher_in_root, AFFINITY_ARABIC_EXE)
            .map_err(|e| map_fs_err(e, "Failed to write AffinityArabic.exe"))?;

        let launcher_path = launcher_in_root.to_string_lossy().to_string();
        if options.create_desktop_shortcut {
            create_desktop_shortcut(&launcher_path, "Affinity (Arabic)");
        }
        launcher_path_out = Some(launcher_path);
    }

    Ok(InstallResponse {
        success: true,
        message: "Affinity Arabic Support installed successfully!".to_string(),
        launcher_path: launcher_path_out,
    })
}

fn create_desktop_shortcut(target_exe: &str, shortcut_name: &str) {
    let target_dir = Path::new(target_exe).parent().unwrap_or(Path::new("")).to_string_lossy();
    let script = format!(
        r#"$ws = New-Object -ComObject WScript.Shell; $desktop = [Environment]::GetFolderPath('Desktop'); $s = $ws.CreateShortcut("$desktop\{}.lnk"); $s.TargetPath = '{}'; $s.IconLocation = '{},0'; $s.WorkingDirectory = '{}'; $s.Save()"#,
        shortcut_name,
        target_exe,
        target_exe,
        target_dir
    );
    let _ = Command::new("powershell")
        .args(["-NoProfile", "-NonInteractive", "-Command", &script])
        .spawn();
}

#[tauri::command]
fn launch_affinity(exe_path: String) -> Result<(), String> {
    let path = Path::new(&exe_path);
    if !path.exists() {
        return Err("Executable not found".to_string());
    }

    let work_dir = path.parent().unwrap_or(Path::new(""));
    Command::new(path)
        .current_dir(work_dir)
        .spawn()
        .map_err(|e| format!("Failed to start process: {}", e))?;

    Ok(())
}

#[tauri::command]
fn open_twitter_url() {
    let url = "https://x.com/HayyanCodes";
    let _ = open::that(url);
}

#[tauri::command]
fn open_support_url() {
    let url = "https://buymeacoffee.com/hayyan";
    let _ = open::that(url);
}

#[tauri::command]
fn minimize_window(window: tauri::Window) {
    let _ = window.minimize();
}

#[tauri::command]
fn close_window(window: tauri::Window) {
    let _ = window.close();
}

#[tauri::command]
fn start_dragging(window: tauri::Window) {
    let _ = window.start_dragging();
}

pub fn run() {
    let app = tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            get_system_info,
            detect_installations,
            browse_folder,
            validate_folder,
            install_package,
            check_existing_install,
            uninstall_package,
            launch_affinity,
            open_twitter_url,
            open_support_url,
            minimize_window,
            close_window,
            start_dragging
        ])
        .build(tauri::generate_context!());

    match app {
        Ok(app) => {
            app.run(|_app_handle, _event| {});
        }
        Err(e) => {
            let err_str = format!("Failed to initialize installer UI:\n\n{}", e);
            rfd::MessageDialog::new()
                .set_level(rfd::MessageLevel::Error)
                .set_title("Affinity Arabic Installer Error")
                .set_description(&err_str)
                .show();
        }
    }
}
