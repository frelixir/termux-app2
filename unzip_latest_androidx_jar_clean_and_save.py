import os
import glob
import zipfile
import re
import shutil

def is_version_folder(name):
    return re.match(r"^\d+(\.\d+)*([\-\.][a-zA-Z0-9]+)?$", name)

def parse_version(v):
    parts = re.split(r'[\.-]', v)
    def val(x): return int(x) if x.isdigit() else x
    return [val(x) for x in parts]

def find_artifact_dirs(base_dir):
    return [os.path.join(base_dir, d) for d in os.listdir(base_dir) if "androidx" in d and os.path.isdir(os.path.join(base_dir, d))]

def find_artifact_subdirs(artifact_dir):
    return [os.path.join(artifact_dir, d) for d in os.listdir(artifact_dir) if os.path.isdir(os.path.join(artifact_dir, d))]

def find_version_dirs(subdir):
    return [d for d in os.listdir(subdir) if is_version_folder(d) and os.path.isdir(os.path.join(subdir, d))]

def find_latest_version(subdir):
    versions = find_version_dirs(subdir)
    if not versions: return None
    latest = sorted(versions, key=parse_version, reverse=True)[0]
    return latest

def find_aar_dir(version_dir, artifact_name, version):
    target_aar = f"{artifact_name}-{version}.aar"
    for root, _, files in os.walk(version_dir):
        for f in files:
            if f == target_aar:
                return os.path.join(root, f)
    return None

def extract_classes_jar_from_aar(aar_path, extract_root):
    jar_paths = []
    try:
        with zipfile.ZipFile(aar_path, 'r') as zip_ref:
            for name in zip_ref.namelist():
                if name.endswith('classes.jar'):
                    os.makedirs(extract_root, exist_ok=True)
                    zip_ref.extract(name, extract_root)
                    jar_path = os.path.join(extract_root, name)
                    jar_paths.append(jar_path)
                    print(f"Extracted {name} from {aar_path} to {jar_path}")
    except Exception as e:
        print(f"解压失败: {aar_path} 错误: {e}")
    return jar_paths

if __name__ == "__main__":
    base_dir = "/home/builder/.gradle/caches/modules-2/files-2.1"
    output_dir = os.path.expanduser("~/libs")
    jar_txt = os.path.expanduser("~/jarPath.txt")

    # 清除历史目录和文件
    if os.path.exists(output_dir):
        shutil.rmtree(output_dir)
        print(f"已删除目录: {output_dir}")
    if os.path.exists(jar_txt):
        os.remove(jar_txt)
        print(f"已删除文件: {jar_txt}")

    artifact_dirs = find_artifact_dirs(base_dir)
    all_jar_paths = []

    for artifact_dir in artifact_dirs:
        subdirs = find_artifact_subdirs(artifact_dir)
        for subdir in subdirs:
            artifact_name = os.path.basename(subdir)
            latest_version = find_latest_version(subdir)
            if not latest_version:
                continue
            version_dir = os.path.join(subdir, latest_version)
            aar_path = find_aar_dir(version_dir, artifact_name, latest_version)
            if aar_path:
                rel_path = os.path.relpath(aar_path, base_dir)
                extract_dir = os.path.join(output_dir, os.path.splitext(rel_path)[0])
                jar_paths = extract_classes_jar_from_aar(aar_path, extract_dir)
                all_jar_paths.extend(jar_paths)

    # 写入到文件
    with open(jar_txt, "w") as f:
        for jar_path in all_jar_paths:
            f.write(jar_path + "\n")
    print(f"所有 classes.jar 路径已写入: {jar_txt}")