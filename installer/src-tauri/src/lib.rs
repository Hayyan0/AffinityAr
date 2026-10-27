use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;
use std::process::Command;
use std::os::windows::process::CommandExt;
use std::time::{Duration, Instant};

const CREATE_NO_WINDOW: u32 = 0x08000000;

fn silent_command<S: AsRef<std::ffi::OsStr>>(program: S) -> Command {
    let mut cmd = Command::new(program);
    cmd.creation_flags(CREATE_NO_WINDOW);
    cmd
}

fn run_with_timeout(cmd: &mut Command, timeout: Duration) -> bool {
    use std::process::Stdio;

    let mut child = match cmd
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
    {
        Ok(child) => child,
        Err(_) => return false,
    };

    let started = Instant::now();
    loop {
        match child.try_wait() {
            Ok(Some(status)) => return status.success(),
            Ok(None) => {
                if started.elapsed() >= timeout {
                    let _ = child.kill();
                    let _ = child.wait();
                    return false;
                }
                std::thread::sleep(Duration::from_millis(25));
            }
            Err(_) => {
                let _ = child.kill();
                let _ = child.wait();
                return false;
            }
        }
    }
}

const AFFINITY_ARABIC_DLL: &[u8] = include_bytes!("../../../bin/affinity_arabic.dll");
const LIBSTACKTRACE_DLL: &[u8] = include_bytes!("../../../bin/libstacktrace.dll");
const AFFINITY_ARABIC_EXE: &[u8] = include_bytes!("../../../bin/AffinityArabic.exe");
const INSTALLER_VERSION: &str = env!("CARGO_PKG_VERSION");

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

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct AffinityInstallation {
    pub name: String,
    pub path: String,
    pub exe_path: String,
    pub version: String,
    pub detected: bool,
    pub install_type: String, 
    pub badge: String,
    pub description: String,
}

#[derive(Deserialize, Debug)]
pub struct InstallOptions {
    pub target_path: String,
    pub install_seamless_dll: bool,
    pub install_launcher: bool,
    pub create_desktop_shortcut: bool,
    pub install_type: Option<String>,
}

#[derive(Serialize)]
pub struct ExistingInstall {
    pub installed: bool,
    pub version: String,
    pub seamless: bool,
    pub launcher: bool,
    pub install_type: String,
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
    "تعذّر الوصول إلى ملفات Affinity. تأكد من إغلاق البرنامج بالكامل، ومن أن المُثبت يعمل بصلاحيات مسؤول، ثم أعد المحاولة.".to_string()
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
    "3.3.x".to_string()
}

fn query_file_description(path: &str) -> Option<String> {
    use std::os::windows::ffi::OsStrExt;
    let wide_path: Vec<u16> = std::ffi::OsStr::new(path).encode_wide().chain(std::iter::once(0)).collect();
    unsafe {
        let size = GetFileVersionInfoSizeW(wide_path.as_ptr(), std::ptr::null_mut());
        if size == 0 {
            return None;
        }
        let mut data = vec![0u8; size as usize];
        if GetFileVersionInfoW(wide_path.as_ptr(), 0, size, data.as_mut_ptr()) == 0 {
            return None;
        }
        let sub_block: Vec<u16> = std::ffi::OsStr::new("\\StringFileInfo\\040904b0\\FileDescription")
            .encode_wide()
            .chain(std::iter::once(0))
            .collect();
        let mut buf_ptr: *mut u8 = std::ptr::null_mut();
        let mut len: u32 = 0;
        if VerQueryValueW(data.as_ptr(), sub_block.as_ptr(), &mut buf_ptr, &mut len) == 0
            || buf_ptr.is_null()
            || len < 2
        {
            return None;
        }
        let units = len as usize / 2;
        let slice = std::slice::from_raw_parts(buf_ptr as *const u16, units);
        let end = slice.iter().position(|&c| c == 0).unwrap_or(slice.len());
        Some(String::from_utf16_lossy(&slice[..end]))
    }
}

fn is_legacy_version_proxy(path: &Path) -> bool {
    if let Some(description) = query_file_description(&path.to_string_lossy()) {
        if description.contains("Affinity Arabic") {
            return true;
        }
    }
    fs::metadata(path)
        .map(|m| m.len() > 200_000 && m.len() < 400_000)
        .unwrap_or(false)
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

fn detect_msix_installations() -> Vec<AffinityInstallation> {
    let mut list = Vec::new();
    let mut seen = std::collections::HashSet::new();

    let ps_cmd = r#"Get-AppxPackage *Affinity* | Select-Object -ExpandProperty InstallLocation"#;
    if let Ok(out) = silent_command("powershell").args(["-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-Command", ps_cmd]).output() {
        if out.status.success() {
            let stdout = String::from_utf8_lossy(&out.stdout);
            for line in stdout.lines() {
                let root = line.trim();
                if !root.is_empty() {
                    let pkg_dir = Path::new(root);
                    let mut exe_path = pkg_dir.join("App").join("Affinity.exe");
                    if !exe_path.exists() {
                        exe_path = pkg_dir.join("Affinity.exe");
                    }
                    if exe_path.exists() {
                        let norm = root.to_lowercase();
                        if seen.insert(norm) {
                            let exe_str = exe_path.to_string_lossy().to_string();
                            let ver = query_real_file_version(&exe_str);
                            list.push(AffinityInstallation {
                                name: "Affinity (نسخة المتجر / MSIX)".to_string(),
                                path: pkg_dir.to_string_lossy().to_string(),
                                exe_path: exe_str,
                                version: ver,
                                detected: true,
                                install_type: "msix".to_string(),
                                badge: "STORE / MSIX".to_string(),
                                description: "تثبيت حزمة تطبيقات ويندوز / متجر مايكروسوفت (MSIX)".to_string(),
                            });
                        }
                    }
                }
            }
        }
    }

    if list.is_empty() {
        let ps_cmd2 = r#"Get-ItemProperty "HKCU:\Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\Repository\Packages\*Affinity*" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty PackageRootFolder"#;
        if let Ok(out) = silent_command("powershell").args(["-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-Command", ps_cmd2]).output() {
            if out.status.success() {
                let stdout = String::from_utf8_lossy(&out.stdout);
                for line in stdout.lines() {
                    let root = line.trim();
                    if !root.is_empty() {
                        let pkg_dir = Path::new(root);
                        let mut exe_path = pkg_dir.join("App").join("Affinity.exe");
                        if !exe_path.exists() {
                            exe_path = pkg_dir.join("Affinity.exe");
                        }
                        if exe_path.exists() {
                            let norm = root.to_lowercase();
                            if seen.insert(norm) {
                                let exe_str = exe_path.to_string_lossy().to_string();
                                let ver = query_real_file_version(&exe_str);
                                list.push(AffinityInstallation {
                                    name: "Affinity (نسخة المتجر / MSIX)".to_string(),
                                    path: pkg_dir.to_string_lossy().to_string(),
                                    exe_path: exe_str,
                                    version: ver,
                                    detected: true,
                                    install_type: "msix".to_string(),
                                    badge: "STORE / MSIX".to_string(),
                                    description: "تثبيت حزمة تطبيقات ويندوز / متجر مايكروسوفت (MSIX)".to_string(),
                                });
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if list.is_empty() {
        let win_apps = Path::new(r"C:\Program Files\WindowsApps");
        if win_apps.is_dir() {
            if let Ok(entries) = fs::read_dir(win_apps) {
                for entry in entries.flatten() {
                    let fname = entry.file_name().to_string_lossy().to_string().to_lowercase();
                    if fname.contains("affinity") {
                        let pkg_dir = entry.path();
                        let mut exe_path = pkg_dir.join("App").join("Affinity.exe");
                        if !exe_path.exists() {
                            exe_path = pkg_dir.join("Affinity.exe");
                        }
                        if exe_path.exists() {
                            let norm = pkg_dir.to_string_lossy().to_lowercase();
                            if seen.insert(norm) {
                                let exe_str = exe_path.to_string_lossy().to_string();
                                let ver = query_real_file_version(&exe_str);
                                list.push(AffinityInstallation {
                                    name: "Affinity (نسخة المتجر / MSIX)".to_string(),
                                    path: pkg_dir.to_string_lossy().to_string(),
                                    exe_path: exe_str,
                                    version: ver,
                                    detected: true,
                                    install_type: "msix".to_string(),
                                    badge: "STORE / MSIX".to_string(),
                                    description: "تثبيت حزمة تطبيقات ويندوز / متجر مايكروسوفت (MSIX)".to_string(),
                                });
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    list
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
    let mut seen_paths = std::collections::HashSet::new();

    let candidates = [
        ("Affinity (نسخة Enterprise)", r"C:\Program Files\Affinity\Affinity"),
        ("Affinity Enterprise (D:)", r"D:\Program Files\Affinity\Affinity"),
        ("Affinity Enterprise (E:)", r"E:\Program Files\Affinity\Affinity"),
        ("Affinity Enterprise (D:)", r"D:\Affinity\Affinity"),
        ("Affinity Enterprise (E:)", r"E:\Affinity\Affinity"),
    ];

    for (name, dir) in candidates {
        let exe_full = Path::new(dir).join("Affinity.exe");
        if exe_full.exists() {
            let norm = dir.to_lowercase();
            if seen_paths.insert(norm) {
                let exe_str = exe_full.to_string_lossy().to_string();
                let ver = query_real_file_version(&exe_str);
                results.push(AffinityInstallation {
                    name: name.to_string(),
                    path: dir.to_string(),
                    exe_path: exe_str,
                    version: ver,
                    detected: true,
                    install_type: "enterprise".to_string(),
                    badge: "ENTERPRISE".to_string(),
                    description: "تثبيت تقليدي عبر ملف EXE/MSI المستقل".to_string(),
                });
            }
        }
    }

    if let Ok(local_app_data) = std::env::var("LOCALAPPDATA") {
        for sub in [r"Programs\Affinity\Affinity", r"Affinity\Affinity"] {
            let user_dir = Path::new(&local_app_data).join(sub);
            let exe_full = user_dir.join("Affinity.exe");
            if exe_full.exists() {
                let dir_str = user_dir.to_string_lossy().to_string();
                let norm = dir_str.to_lowercase();
                if seen_paths.insert(norm) {
                    let exe_str = exe_full.to_string_lossy().to_string();
                    let ver = query_real_file_version(&exe_str);
                    results.push(AffinityInstallation {
                        name: "Affinity (نسخة Enterprise / User)".to_string(),
                        path: dir_str,
                        exe_path: exe_str,
                        version: ver,
                        detected: true,
                        install_type: "enterprise".to_string(),
                        badge: "ENTERPRISE".to_string(),
                        description: "تثبيت تقليدي في حساب المستخدم الحالي".to_string(),
                    });
                }
            }
        }
    }

    let app_paths_cmd = r#"Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\Affinity.exe", "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\Affinity.exe" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty '(default)'"#;
    if let Ok(out) = silent_command("powershell").args(["-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-Command", app_paths_cmd]).output() {
        if out.status.success() {
            let stdout = String::from_utf8_lossy(&out.stdout);
            for line in stdout.lines() {
                let exe_str = line.trim().trim_matches('"');
                let exe_path = Path::new(exe_str);
                if exe_path.exists() && !exe_str.to_lowercase().contains("windowsapps") {
                    if let Some(parent) = exe_path.parent() {
                        let dir_str = parent.to_string_lossy().to_string();
                        let norm = dir_str.to_lowercase();
                        if seen_paths.insert(norm) {
                            let ver = query_real_file_version(exe_str);
                            results.push(AffinityInstallation {
                                name: "Affinity (نسخة Enterprise / MSI)".to_string(),
                                path: dir_str,
                                exe_path: exe_str.to_string(),
                                version: ver,
                                detected: true,
                                install_type: "enterprise".to_string(),
                                badge: "ENTERPRISE".to_string(),
                                description: "تثبيت تقليدي مسجل في النظام عبر ملف EXE".to_string(),
                            });
                        }
                    }
                }
            }
        }
    }

    let msix_list = detect_msix_installations();
    for msix in msix_list {
        let norm = msix.path.to_lowercase();
        if seen_paths.insert(norm) {
            results.push(msix);
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

    let is_msix_path = folder_path.to_lowercase().contains("windowsapps")
        || folder_path.to_lowercase().contains("affinitymsix")
        || path.join("AppxManifest.xml").exists()
        || path.join("..").join("AppxManifest.xml").exists();

    if is_msix_path {
        let mut target_exe = path.join("App").join("Affinity.exe");
        if !target_exe.exists() {
            target_exe = path.join("Affinity.exe");
        }
        if target_exe.exists() {
            let exe_str = target_exe.to_string_lossy().to_string();
            let ver = query_real_file_version(&exe_str);
            return AffinityInstallation {
                name: "Affinity (نسخة المتجر / MSIX)".to_string(),
                path: folder_path,
                exe_path: exe_str,
                version: ver,
                detected: true,
                install_type: "msix".to_string(),
                badge: "STORE / MSIX".to_string(),
                description: "مجلد حزمة تطبيقات ويندوز (MSIX)".to_string(),
            };
        }
    }

    let target_exe = path.join("Affinity.exe");
    if target_exe.exists() {
        let exe_str = target_exe.to_string_lossy().to_string();
        let ver = query_real_file_version(&exe_str);
        return AffinityInstallation {
            name: "Affinity (نسخة Enterprise / MSI)".to_string(),
            path: folder_path,
            exe_path: exe_str,
            version: ver,
            detected: true,
            install_type: "enterprise".to_string(),
            badge: "ENTERPRISE".to_string(),
            description: "مجلد تثبيت صالح لنسخة Enterprise (EXE/MSI)".to_string(),
        };
    }

    AffinityInstallation {
        name: "Custom Directory".to_string(),
        path: folder_path,
        exe_path: "".to_string(),
        version: "".to_string(),
        detected: false,
        install_type: "unknown".to_string(),
        badge: "UNKNOWN".to_string(),
        description: "المجلد المحدد لا يحتوي على ملف Affinity.exe".to_string(),
    }
}

#[tauri::command]
fn check_existing_install(target_path: String) -> ExistingInstall {
    let target = Path::new(&target_path);

    let is_msix = target_path.to_lowercase().contains("windowsapps")
        || target_path.to_lowercase().contains("affinitymsix")
        || target.join("AppxManifest.xml").exists();

    if is_msix {
        let mut launcher_installed = false;
        let mut seamless_installed = false;

        let run_check = silent_command("reg")
            .args(["query", r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run", "/v", "AffinityArabic"])
            .output();
        if let Ok(out) = run_check {
            if out.status.success() {
                seamless_installed = true;
            }
        }

        if let Ok(local_app_data) = std::env::var("LOCALAPPDATA") {
            let launcher_file = Path::new(&local_app_data).join("AffinityArabic").join("AffinityArabic.exe");
            if launcher_file.exists() {
                launcher_installed = true;
            }
        }

        let installed = launcher_installed || seamless_installed;
        let mut version = String::new();
        if let Ok(local_app_data) = std::env::var("LOCALAPPDATA") {
            let marker = Path::new(&local_app_data).join("AffinityArabic").join("version.txt");
            if let Ok(content) = fs::read_to_string(marker) {
                version = content.trim().to_string();
            }
        }
        return ExistingInstall {
            installed,
            version,
            seamless: seamless_installed,
            launcher: launcher_installed,
            install_type: "msix".to_string(),
        };
    }

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
            install_type: "enterprise".to_string(),
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
        install_type: "enterprise".to_string(),
    }
}

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

    let is_msix = target_path.to_lowercase().contains("windowsapps")
        || target_path.to_lowercase().contains("affinitymsix")
        || target.join("AppxManifest.xml").exists();

    if is_msix {
        let mut taskkill = silent_command("taskkill");
        taskkill.args(["/F", "/IM", "AffinityArabic.exe"]);
        let _ = run_with_timeout(&mut taskkill, Duration::from_secs(5));

        let _ = silent_command("reg")
            .args(["delete", r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run", "/v", "AffinityArabic", "/f"])
            .output();

        let _ = silent_command("reg")
            .args(["delete", r"HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\Affinity.exe", "/f"])
            .output();

        let _ = remove_desktop_shortcut("Affinity (Arabic)");

        if let Ok(local_app_data) = std::env::var("LOCALAPPDATA") {
            let launcher_dir = Path::new(&local_app_data).join("AffinityArabic");
            if launcher_dir.exists() {
                let _ = fs::remove_dir_all(&launcher_dir);
            }
        }

        return Ok(UninstallResponse {
            success: true,
            message: "تمت إزالة تعريب نسخة MSIX بنجاح واسترجاع إعدادات النظام الأصلية بالكامل.".to_string(),
        });
    }

    if !target.exists() {
        return Err(format!("Selected folder does not exist: {}", target_path));
    }

    let support_dir = target.join("arabic_support");
    let has_install_marker = support_dir.join("version.txt").exists()
        || support_dir.join("bin").join("affinity_arabic.dll").exists()
        || target.join("affinity_arabic.dll").exists()
        || target.join("libstacktrace_orig.dll").exists()
        || target.join("AffinityArabic.exe").exists();
    if !target.join("Affinity.exe").exists() || !has_install_marker {
        return Err("المجلد المحدد لا يحتوي على تثبيت تعريب Affinity صالح.".to_string());
    }

    let version_dll_path = target.join("version.dll");
    if version_dll_path.exists() && is_legacy_version_proxy(&version_dll_path) {
        let _ = fs::remove_file(&version_dll_path);
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

fn ps_escape(value: &str) -> String {
    value.replace('\'', "''")
}

fn remove_desktop_shortcut(shortcut_name: &str) -> Result<(), String> {
    let name = ps_escape(shortcut_name);
    let script = format!(
        r#"$d = Join-Path ([Environment]::GetFolderPath('Desktop')) '{}.lnk'; if (Test-Path $d) {{ Remove-Item -LiteralPath $d -Force }}; $p = Join-Path ([Environment]::GetFolderPath('Programs')) '{}.lnk'; if (Test-Path $p) {{ Remove-Item -LiteralPath $p -Force }}"#,
        name, name
    );
    silent_command("powershell")
        .args(["-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-Command", &script])
        .output()
        .map_err(|e| e.to_string())?;

    Ok(())
}

#[tauri::command(async)]
fn install_package(options: InstallOptions) -> Result<InstallResponse, String> {
    let target_dir = Path::new(&options.target_path);
    if !target_dir.exists() {
        return Err(format!("المجلد المحدد غير موجود: {}", options.target_path));
    }

    let is_msix = options.install_type.as_deref() == Some("msix")
        || options.target_path.to_lowercase().contains("windowsapps")
        || options.target_path.to_lowercase().contains("affinitymsix")
        || target_dir.join("AppxManifest.xml").exists();

    if is_msix {
        if !target_dir.join("Affinity.exe").exists() && !target_dir.join("App").join("Affinity.exe").exists() {
            return Err("المجلد المحدد لا يحتوي على Affinity.exe لنسخة MSIX.".to_string());
        }
    } else if !target_dir.join("Affinity.exe").exists() {
        return Err("المجلد المحدد لا يحتوي على Affinity.exe. التعريب يدعم نسخة Affinity الموحدة فقط.".to_string());
    }

    if options.install_seamless_dll == options.install_launcher {
        return Err("اختر طريقة تثبيت واحدة فقط: دمج تلقائي أو مشغّل مستقل.".to_string());
    }

    if is_msix {
        let local_app_data = std::env::var("LOCALAPPDATA").unwrap_or_else(|_| r"C:\Users\Public".to_string());
        let launcher_dir = Path::new(&local_app_data).join("AffinityArabic");
        fs::create_dir_all(&launcher_dir)
            .map_err(|e| map_fs_err(e, "تعذر إنشاء مجلد التعريب لنسخة MSIX"))?;

        let mut taskkill = silent_command("taskkill");
        taskkill.args(["/F", "/IM", "AffinityArabic.exe"]);
        let _ = run_with_timeout(&mut taskkill, Duration::from_secs(5));

        let launcher_exe = launcher_dir.join("AffinityArabic.exe");
        fs::write(&launcher_exe, AFFINITY_ARABIC_EXE)
            .map_err(|e| map_fs_err(e, "تعذر كتابة AffinityArabic.exe"))?;

        let hook_dll = launcher_dir.join("affinity_arabic.dll");
        fs::write(&hook_dll, AFFINITY_ARABIC_DLL)
            .map_err(|e| map_fs_err(e, "تعذر كتابة affinity_arabic.dll"))?;

        let mut icacls = silent_command("icacls");
        icacls.args([
            &launcher_dir.to_string_lossy(),
            "/grant",
            "*S-1-15-2-1:(OI)(CI)M",
            "/T",
            "/Q",
        ]);
        let _ = run_with_timeout(&mut icacls, Duration::from_secs(10));

        let target_exe = if target_dir.join("Affinity.exe").exists() {
            target_dir.join("Affinity.exe")
        } else {
            target_dir.join("App").join("Affinity.exe")
        };
        if !target_exe.exists() {
            return Err("تعذر العثور على Affinity.exe داخل حزمة MSIX.".to_string());
        }
        let cfg_file = launcher_dir.join("affinity_msix.cfg");
        fs::write(&cfg_file, target_exe.to_string_lossy().as_bytes())
            .map_err(|e| map_fs_err(e, "تعذر كتابة إعدادات مسار Affinity"))?;

        let version_marker = launcher_dir.join("version.txt");
        fs::write(&version_marker, INSTALLER_VERSION)
            .map_err(|e| map_fs_err(e, "تعذر كتابة ملف إصدار التعريب"))?;

        let launcher_path = launcher_exe.to_string_lossy().to_string();

        if options.install_seamless_dll {
            let reg_val = format!(r#"\"{}\" --watch"#, launcher_path);
            let mut reg_add = silent_command("reg");
            reg_add.args(["add", r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run", "/v", "AffinityArabic", "/t", "REG_SZ", "/d", &reg_val, "/f"]);
            let _ = run_with_timeout(&mut reg_add, Duration::from_secs(5));

            let _ = silent_command(&launcher_path).arg("--watch").spawn();

            create_desktop_shortcut(&launcher_path, "Affinity (Arabic)");

            return Ok(InstallResponse {
                success: true,
                message: "تم دمج التعريب التلقائي لنسخة المتجر (MSIX) بنجاح! يعمل التعريب الآن في الخلفية وسيتفعل تلقائياً عند تشغيل Affinity من قائمة ابدأ أو شريط المهام أو أي ملف.".to_string(),
                launcher_path: Some(launcher_path),
            });
        }

        if options.install_launcher {
            let mut reg_delete = silent_command("reg");
            reg_delete.args(["delete", r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run", "/v", "AffinityArabic", "/f"]);
            let _ = run_with_timeout(&mut reg_delete, Duration::from_secs(5));

            if options.create_desktop_shortcut {
                create_desktop_shortcut(&launcher_path, "Affinity (Arabic)");
            }

            return Ok(InstallResponse {
                success: true,
                message: "تم تثبيت مشغّل التعريب لنسخة المتجر (MSIX) بنجاح! يمكنك الآن تشغيل البرنامج المعرب عبر اختصار سطح المكتب.".to_string(),
                launcher_path: Some(launcher_path),
            });
        }

        return Err("لم يتم اختيار طريقة تثبيت صالحة لنسخة MSIX.".to_string());
    }
    let arabic_support_dir = target_dir.join("arabic_support");
    let bin_dir = arabic_support_dir.join("bin");
    let backup_dir = arabic_support_dir.join("backup");

    fs::create_dir_all(&bin_dir)
        .map_err(|e| map_fs_err(e, &format!("Failed to create directory {}", bin_dir.display())))?;
    fs::create_dir_all(&backup_dir)
        .map_err(|e| map_fs_err(e, "Failed to create backup directory"))?;

    let version_marker = arabic_support_dir.join("version.txt");
    fs::write(&version_marker, INSTALLER_VERSION)
        .map_err(|e| map_fs_err(e, "Failed to write version marker"))?;

    let core_dll_path = bin_dir.join("affinity_arabic.dll");
    fs::write(&core_dll_path, AFFINITY_ARABIC_DLL)
        .map_err(|e| map_fs_err(e, "Failed to write affinity_arabic.dll"))?;

    let legacy_root_dll = target_dir.join("affinity_arabic.dll");
    if legacy_root_dll.exists() {
        let _ = fs::remove_file(&legacy_root_dll);
    }

    let version_dll_path = target_dir.join("version.dll");
    if version_dll_path.exists() && is_legacy_version_proxy(&version_dll_path) {
        let _ = fs::remove_file(&version_dll_path);
    }

    let _ = fs::remove_file(arabic_support_dir.join("hook.log"));
    let _ = fs::remove_file(target_dir.join("hook.log"));

    if options.install_seamless_dll {
        let orig_libstacktrace = target_dir.join("libstacktrace_orig.dll");
        let target_libstacktrace = target_dir.join("libstacktrace.dll");
        let backup_libstacktrace = backup_dir.join("libstacktrace.dll.bak");

        if !orig_libstacktrace.exists() && target_libstacktrace.exists() {
            let already_proxy = fs::read(&target_libstacktrace)
                .map(|bytes| bytes == LIBSTACKTRACE_DLL)
                .unwrap_or(false);
            if already_proxy {
                return Err("ملف libstacktrace.dll الحالي هو نسخة تعريب ولا توجد نسخة أصلية محفوظة. أعد تثبيت Affinity ثم حاول مجددًا.".to_string());
            }
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
        fs::write(&launcher_in_bin, AFFINITY_ARABIC_EXE)
            .map_err(|e| map_fs_err(e, "Failed to write AffinityArabic.exe"))?;

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
        message: "تم تثبيت دعم اللغة العربية لـ Affinity بنجاح!".to_string(),
        launcher_path: launcher_path_out,
    })
}

fn create_desktop_shortcut(target_exe: &str, shortcut_name: &str) {
    let target_dir = Path::new(target_exe).parent().unwrap_or(Path::new("")).to_string_lossy();
    let name = ps_escape(shortcut_name);
    let target = ps_escape(target_exe);
    let workdir = ps_escape(&target_dir);
    let script = format!(
        r#"$ws = New-Object -ComObject WScript.Shell;
$desktop = [Environment]::GetFolderPath('Desktop');
$s = $ws.CreateShortcut("$desktop\{}.lnk");
$s.TargetPath = '{}';
$s.IconLocation = '{},0';
$s.WorkingDirectory = '{}';
$s.Save();
$programs = [Environment]::GetFolderPath('Programs');
$s2 = $ws.CreateShortcut("$programs\{}.lnk");
$s2.TargetPath = '{}';
$s2.IconLocation = '{},0';
$s2.WorkingDirectory = '{}';
$s2.Save();"#,
        name, target, target, workdir,
        name, target, target, workdir
    );
    let mut powershell = silent_command("powershell");
    powershell.args(["-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-Command", &script]);
    let _ = run_with_timeout(&mut powershell, Duration::from_secs(10));
}

fn package_family_name(package_root: &Path) -> Option<String> {
    let dir_name = package_root.file_name()?.to_string_lossy();
    let parts: Vec<&str> = dir_name.split('_').collect();
    if parts.len() < 4 || parts[parts.len() - 1].is_empty() {
        return None;
    }
    Some(format!("{}_{}", parts[0], parts[parts.len() - 1]))
}

fn package_application_id(package_root: &Path) -> Option<String> {
    let manifest = fs::read_to_string(package_root.join("AppxManifest.xml")).ok()?;
    let marker = "Application Id=\"";
    let start = manifest.find(marker)? + marker.len();
    let end = manifest[start..].find('"')? + start;
    let id = &manifest[start..end];
    if id.is_empty() { None } else { Some(id.to_string()) }
}

fn launch_packaged_affinity(exe_path: &Path) -> Result<(), String> {
    let mut package_root = exe_path.parent().unwrap_or(Path::new(""));
    if package_root
        .file_name()
        .map(|n| n.to_string_lossy().eq_ignore_ascii_case("App"))
        .unwrap_or(false)
    {
        package_root = package_root.parent().unwrap_or(package_root);
    }
    let family = package_family_name(package_root)
        .ok_or_else(|| "تعذر تحديد هوية حزمة Affinity (MSIX).".to_string())?;
    let app_id = package_application_id(package_root).unwrap_or_else(|| "Canva.Affinity".to_string());
    let target = format!("shell:AppsFolder\\{}!{}", family, app_id);

    silent_command("explorer.exe")
        .arg(target)
        .spawn()
        .map_err(|e| format!("تعذر تشغيل Affinity عبر حزمة MSIX: {}", e))?;
    Ok(())
}

#[tauri::command]
fn launch_affinity(exe_path: String, install_type: Option<String>) -> Result<(), String> {
    let path = Path::new(&exe_path);
    if !path.exists() {
        return Err("Executable not found".to_string());
    }

    let is_msix = install_type.as_deref() == Some("msix")
        || exe_path.to_lowercase().contains("windowsapps")
        || exe_path.to_lowercase().contains("affinitymsix");
    if is_msix {
        return launch_packaged_affinity(path);
    }

    let work_dir = path.parent().unwrap_or(Path::new(""));
    silent_command(path)
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
