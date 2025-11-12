#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
文本文件聚合器
从指定文件夹及其子文件夹中提取所有文本文件，合并到一个markdown文件中
"""

import os
import sys
import argparse
from pathlib import Path
from typing import List, Set, Dict
import chardet
import mimetypes
import hashlib

class TextAggregator:
    """文本文件聚合器类"""
    
    def __init__(self, folder_path: str, output_file: str = "aggregated_texts.md"):
        """
        初始化聚合器
        
        Args:
            folder_path: 要处理的文件夹路径
            output_file: 输出的markdown文件名
        """
        self.folder_path = Path(folder_path)
        self.output_file = Path(output_file)
        
        # 定义文本文件扩展名
        self.text_extensions = {
            '.txt', '.md', '.py', '.js', '.ts', '.html', '.css', '.json', 
            '.xml', '.yaml', '.yml', '.ini', '.cfg', '.conf',
            '.sql', '.sh', '.bat', '.ps1', '.csv', '.tsv', '.rst',
            '.php', '.java', '.cpp', '.c', '.h', '.hpp', '.cs', '.vb',
            '.rb', '.go', '.rs', '.swift', '.kt', '.scala', '.pl', '.r',
            '.tex', '.bib', '.dockerfile'# , '.gitignore', '.gitattributes', '.log'
        }
        
        # self.text_extensions = [
        #     '.txt', '.md' 
        # ]
        
        # 需要排除的文件
        self.exclude_files = {
            '.gitignore', '.gitattributes', '.log', 'package-lock.json', 'TODO.md'
        }
        
        # 需要排除的文件夹
        self.exclude_dirs = {
            '.git', '.svn', '.hg', '__pycache__', '.pytest_cache',
            'node_modules', '.vscode', '.idea', '.vs', 'build', 'dist',
            '.DS_Store', 'Thumbs.db', 'package-lock.json', 'vcpkg', 'external',
            'vcpkg_installed', 'old', 'prompts', 'text_aggregated', 'build-tracy-off',
            'build-tracy-on', 'build-headless-debug'
        }
        
        self.processed_files = []
        self.failed_files = []
        # 用于存储文件哈希值和重复文件映射
        self.file_hashes: Dict[str, Path] = {}  # hash -> first_file_path
        self.duplicate_files: Dict[Path, Path] = {}  # duplicate_file -> original_file
    
    def calculate_file_hash(self, file_path: Path) -> str:
        """
        计算文件的SHA256哈希值
        
        Args:
            file_path: 文件路径
            
        Returns:
            str: 文件的哈希值
        """
        sha256_hash = hashlib.sha256()
        try:
            with open(file_path, "rb") as f:
                # 分块读取文件以处理大文件
                for chunk in iter(lambda: f.read(4096), b""):
                    sha256_hash.update(chunk)
            return sha256_hash.hexdigest()
        except (IOError, OSError) as e:
            print(f"计算文件哈希失败: {file_path} - {e}")
            return ""
    
    def check_duplicate(self, file_path: Path) -> bool:
        """
        检查文件是否为重复文件
        
        Args:
            file_path: 文件路径
            
        Returns:
            bool: 如果是重复文件返回True
        """
        file_hash = self.calculate_file_hash(file_path)
        if not file_hash:
            return False
        
        if file_hash in self.file_hashes:
            # 发现重复文件
            original_file = self.file_hashes[file_hash]
            self.duplicate_files[file_path] = original_file
            return True
        else:
            # 记录新文件的哈希值
            self.file_hashes[file_hash] = file_path
            return False
    
    def is_text_file(self, file_path: Path) -> bool:
        """
        判断文件是否为文本文件
        
        Args:
            file_path: 文件路径
            
        Returns:
            bool: 如果是文本文件返回True
        """
        # 检查文件名
        if file_path.name in self.exclude_files:
            return False
        
        # 检查扩展名
        # print(file_path.suffix.lower(), self.text_extensions, file_path.suffix.lower() in self.text_extensions)
        if file_path.suffix.lower() in self.text_extensions:
            return True
        # else: return False
        
        
        
        # 检查MIME类型
        mime_type, _ = mimetypes.guess_type(str(file_path))
        if mime_type and mime_type.startswith('text/'):
            return True
        
        # 对于没有扩展名的文件，尝试检测内容
        if not file_path.suffix:
            try:
                with open(file_path, 'rb') as f:
                    sample = f.read(1024)
                    if sample:
                        # 检测编码
                        result = chardet.detect(sample)
                        if result['confidence'] > 0.7:
                            return True
            except (IOError, OSError):
                pass
        
        return False
    
    def read_file_content(self, file_path: Path) -> str:
        """
        读取文件内容
        
        Args:
            file_path: 文件路径
            
        Returns:
            str: 文件内容
        """
        encodings = ['utf-8', 'gbk', 'gb2312', 'latin-1', 'cp1252']
        
        for encoding in encodings:
            try:
                with open(file_path, 'r', encoding=encoding) as f:
                    return f.read()
            except (UnicodeDecodeError, UnicodeError):
                continue
            except (IOError, OSError) as e:
                print(f"读取文件失败: {file_path} - {e}")
                return f"[读取失败: {e}]"
        
        # 如果所有编码都失败，尝试自动检测
        try:
            with open(file_path, 'rb') as f:
                raw_data = f.read()
                result = chardet.detect(raw_data)
                if result['encoding']:
                    return raw_data.decode(result['encoding'], errors='ignore')
        except Exception as e:
            print(f"自动检测编码失败: {file_path} - {e}")
        
        return "[无法读取文件内容]"
    
    def get_text_files(self) -> List[Path]:
        """
        递归获取所有文本文件
        
        Returns:
            List[Path]: 文本文件路径列表
        """
        text_files = []
        
        for root, dirs, files in os.walk(self.folder_path):
            # 过滤掉排除的目录
            dirs[:] = [d for d in dirs if d not in self.exclude_dirs]
            
            root_path = Path(root)
            
            for file in files:
                file_path = root_path / file
                # print(file_path, self.is_text_file(file_path))
                if self.is_text_file(file_path):
                    text_files.append(file_path)
        
        return sorted(text_files)
    
    def generate_markdown(self) -> None:
        """
        生成markdown文件
        """
        text_files = self.get_text_files()
        
        if not text_files:
            print("未找到任何文本文件")
            return
        
        print(f"找到 {len(text_files)} 个文本文件")
        
        # 检测重复文件
        unique_files = []
        duplicate_count = 0
        
        for file_path in text_files:
            if not self.check_duplicate(file_path):
                unique_files.append(file_path)
            else:
                duplicate_count += 1
        
        print(f"发现 {duplicate_count} 个重复文件")
        print(f"将处理 {len(unique_files)} 个唯一文件")
        
        with open(self.output_file, 'w', encoding='utf-8') as md_file:
            # 写入标题
            md_file.write("# 文本文件聚合报告\n\n")
            md_file.write(f"**源文件夹**: `{self.folder_path.absolute()}`\n\n")
            md_file.write(f"**生成时间**: {self.get_current_time()}\n\n")
            md_file.write(f"**文件总数**: {len(text_files)}\n\n")
            md_file.write(f"**唯一文件**: {len(unique_files)}\n\n")
            md_file.write(f"**重复文件**: {duplicate_count}\n\n")
            
            # 写入目录
            md_file.write("## 目录\n\n")
            file_index = 1
            
            for file_path in text_files:
                relative_path = file_path.relative_to(self.folder_path)
                
                if file_path in self.duplicate_files:
                    # 重复文件，显示指向原文件的说明
                    original_file = self.duplicate_files[file_path]
                    original_relative = original_file.relative_to(self.folder_path)
                    md_file.write(f"- **{relative_path}** *(重复文件，内容与 [{original_relative}](#{self.get_anchor(original_relative)}) 相同)*\n")
                else:
                    # 唯一文件，正常显示
                    md_file.write(f"{file_index}. [{relative_path}](#{self.get_anchor(relative_path)})\n")
                    file_index += 1
            
            md_file.write("\n")
            
            # 写入文件内容（只写入唯一文件）
            for i, file_path in enumerate(unique_files, 1):
                relative_path = file_path.relative_to(self.folder_path)
                
                try:
                    content = self.read_file_content(file_path)
                    
                    # 写入文件标题
                    md_file.write(f"## {i}. {relative_path}\n\n")
                    md_file.write(f"**文件路径**: `{relative_path}`\n\n")
                    md_file.write(f"**文件大小**: {self.get_file_size(file_path)}\n\n")
                    
                    # 检查是否有重复文件指向这个文件
                    duplicates = [dup_path for dup_path, orig_path in self.duplicate_files.items() if orig_path == file_path]
                    if duplicates:
                        md_file.write("**重复文件**:\n")
                        for dup_path in duplicates:
                            dup_relative = dup_path.relative_to(self.folder_path)
                            md_file.write(f"- `{dup_relative}`\n")
                        md_file.write("\n")
                    
                    # 写入文件内容
                    if content.strip():
                        # 检测文件类型以确定代码块语言
                        language = self.get_language_from_extension(file_path.suffix)
                        md_file.write(f"```{language}\n{content}\n```\n\n")
                    else:
                        md_file.write("*文件为空*\n\n")
                    
                    md_file.write("---\n\n")
                    self.processed_files.append(relative_path)
                    
                except Exception as e:
                    print(f"处理文件失败: {file_path} - {e}")
                    md_file.write(f"**错误**: 无法处理文件 - {e}\n\n")
                    md_file.write("---\n\n")
                    self.failed_files.append(relative_path)
        
        print(f"markdown文件已生成: {self.output_file.absolute()}")
        print(f"成功处理: {len(self.processed_files)} 个文件")
        if self.failed_files:
            print(f"失败文件: {len(self.failed_files)} 个")
        if duplicate_count > 0:
            print(f"跳过重复文件: {duplicate_count} 个")
    
    def get_current_time(self) -> str:
        """获取当前时间"""
        from datetime import datetime
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    def get_file_size(self, file_path: Path) -> str:
        """获取文件大小"""
        try:
            size = file_path.stat().st_size
            if size < 1024:
                return f"{size} bytes"
            elif size < 1024 * 1024:
                return f"{size / 1024:.1f} KB"
            else:
                return f"{size / (1024 * 1024):.1f} MB"
        except:
            return "未知"
    
    def get_language_from_extension(self, ext: str) -> str:
        """根据文件扩展名获取代码语言"""
        ext_map = {
            '.py': 'python',
            '.js': 'javascript',
            '.ts': 'typescript',
            '.html': 'html',
            '.css': 'css',
            '.json': 'json',
            '.xml': 'xml',
            '.yaml': 'yaml',
            '.yml': 'yaml',
            '.sql': 'sql',
            '.sh': 'bash',
            '.bat': 'batch',
            '.ps1': 'powershell',
            '.php': 'php',
            '.java': 'java',
            '.cpp': 'cpp',
            '.c': 'c',
            '.cs': 'csharp',
            '.rb': 'ruby',
            '.go': 'go',
            '.rs': 'rust',
            '.swift': 'swift',
            '.kt': 'kotlin',
            '.scala': 'scala',
            '.r': 'r',
            '.tex': 'latex',
            '.md': 'markdown',
            '.rst': 'rst'
        }
        return ext_map.get(ext.lower(), 'text')
    
    def get_anchor(self, path: Path) -> str:
        """生成markdown锚点"""
        return str(path).replace('/', '-').replace('\\', '-').replace(' ', '-').replace('.', '-').lower()


def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='文本文件聚合器 - 提取文件夹中的文本文件并合并到markdown中')
    parser.add_argument('folder', help='要处理的文件夹路径')
    parser.add_argument('-o', '--output', default='aggregated_texts.md', help='输出markdown文件名（默认: aggregated_texts.md）')
    parser.add_argument('--encoding', default='utf-8', help='输出文件编码（默认: utf-8）')
    
    args = parser.parse_args()
    
    # 检查文件夹是否存在
    if not os.path.exists(args.folder):
        print(f"错误: 文件夹 '{args.folder}' 不存在")
        sys.exit(1)
    
    if not os.path.isdir(args.folder):
        print(f"错误: '{args.folder}' 不是一个文件夹")
        sys.exit(1)
    
    # 创建聚合器并运行
    aggregator = TextAggregator(args.folder, args.output)
    
    try:
        aggregator.generate_markdown()
    except KeyboardInterrupt:
        print("\n程序被用户中断")
        sys.exit(1)
    except Exception as e:
        print(f"程序执行出错: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main() 