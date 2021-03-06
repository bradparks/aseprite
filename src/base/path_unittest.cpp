// Aseprite Base Library
// Copyright (c) 2001-2013 David Capello
//
// This source file is distributed under MIT license,
// please read LICENSE.txt for more information.

#include <gtest/gtest.h>

#include "base/path.h"

using namespace base;

TEST(Path, IsPathSeparator)
{
  EXPECT_TRUE (is_path_separator('\\'));
  EXPECT_TRUE (is_path_separator('/'));
  EXPECT_FALSE(is_path_separator('a'));
  EXPECT_FALSE(is_path_separator('+'));
  EXPECT_FALSE(is_path_separator(':'));
}

TEST(Path, GetFilePath)
{
  EXPECT_EQ("C:\\foo",  get_file_path("C:\\foo\\main.cpp"));
  EXPECT_EQ("C:/foo",   get_file_path("C:/foo/pack.tar.gz"));
  EXPECT_EQ(".",        get_file_path("./main.cpp"));
  EXPECT_EQ(".",        get_file_path(".\\main.cpp"));
  EXPECT_EQ("",         get_file_path("\\main.cpp"));
  EXPECT_EQ("",         get_file_path("main.cpp"));
  EXPECT_EQ("",         get_file_path("main."));
  EXPECT_EQ("",         get_file_path("main"));
  EXPECT_EQ("C:/foo",   get_file_path("C:/foo/"));
  EXPECT_EQ("C:",       get_file_path("C:\\"));
  EXPECT_EQ("C:",       get_file_path("C:\\.cpp"));
  EXPECT_EQ("",         get_file_path(".cpp"));
  EXPECT_EQ("",         get_file_path(""));
}

TEST(Path, GetFileName)
{
  EXPECT_EQ("main.cpp",         get_file_name("C:\\foo\\main.cpp"));
  EXPECT_EQ("pack.tar.gz",      get_file_name("C:/foo/pack.tar.gz"));
  EXPECT_EQ("main.cpp",         get_file_name("./main.cpp"));
  EXPECT_EQ("main.cpp",         get_file_name(".\\main.cpp"));
  EXPECT_EQ("main.cpp",         get_file_name("\\main.cpp"));
  EXPECT_EQ("main.cpp",         get_file_name("main.cpp"));
  EXPECT_EQ("main.",            get_file_name("main."));
  EXPECT_EQ("main",             get_file_name("main"));
  EXPECT_EQ("",                 get_file_name("C:/foo/"));
  EXPECT_EQ("",                 get_file_name("C:\\"));
  EXPECT_EQ(".cpp",             get_file_name("C:\\.cpp"));
  EXPECT_EQ(".cpp",             get_file_name(".cpp"));
  EXPECT_EQ("",                 get_file_name(""));
}

TEST(Path, GetFileExtension)
{
  EXPECT_EQ("cpp",      get_file_extension("C:\\foo\\main.cpp"));
  EXPECT_EQ("gz",       get_file_extension("C:/foo/pack.tar.gz"));
  EXPECT_EQ("cpp",      get_file_extension("./main.cpp"));
  EXPECT_EQ("cpp",      get_file_extension(".\\main.cpp"));
  EXPECT_EQ("cpp",      get_file_extension("\\main.cpp"));
  EXPECT_EQ("cpp",      get_file_extension("main.cpp"));
  EXPECT_EQ("",         get_file_extension("main."));
  EXPECT_EQ("",         get_file_extension("main"));
  EXPECT_EQ("",         get_file_extension("C:/foo/"));
  EXPECT_EQ("",         get_file_extension("C:\\"));
  EXPECT_EQ("cpp",      get_file_extension("C:\\.cpp"));
  EXPECT_EQ("cpp",      get_file_extension(".cpp"));
  EXPECT_EQ("",         get_file_extension(""));
}

TEST(Path, GetFileTitle)
{
  EXPECT_EQ("main",     get_file_title("C:\\foo\\main.cpp"));
  EXPECT_EQ("pack.tar", get_file_title("C:/foo/pack.tar.gz"));
  EXPECT_EQ("main",     get_file_title("./main.cpp"));
  EXPECT_EQ("main",     get_file_title(".\\main.cpp"));
  EXPECT_EQ("main",     get_file_title("\\main.cpp"));
  EXPECT_EQ("main",     get_file_title("main.cpp"));
  EXPECT_EQ("main",     get_file_title("main."));
  EXPECT_EQ("main",     get_file_title("main"));
  EXPECT_EQ("",         get_file_title("C:/foo/"));
  EXPECT_EQ("",         get_file_title("C:\\"));
  EXPECT_EQ("",         get_file_title("C:\\.cpp"));
  EXPECT_EQ("",         get_file_title(".cpp"));
  EXPECT_EQ("",         get_file_title(""));
}

TEST(Path, JoinPath)
{
  base::string sep;
  sep.push_back(path_separator);

  EXPECT_EQ("",                         join_path("", ""));
  EXPECT_EQ("fn",                       join_path("", "fn"));
  EXPECT_EQ("/fn",                      join_path("/", "fn"));
  EXPECT_EQ("/this"+sep+"fn",           join_path("/this", "fn"));
  EXPECT_EQ("C:\\path"+sep+"fn",        join_path("C:\\path", "fn"));
  EXPECT_EQ("C:\\path\\fn",             join_path("C:\\path\\", "fn"));
}

TEST(Path, RemovePathSeparator)
{
  EXPECT_EQ("C:\\foo",                  remove_path_separator("C:\\foo\\"));
  EXPECT_EQ("C:/foo",                   remove_path_separator("C:/foo/"));
  EXPECT_EQ("C:\\foo\\main.cpp",        remove_path_separator("C:\\foo\\main.cpp"));
  EXPECT_EQ("C:\\foo\\main.cpp",        remove_path_separator("C:\\foo\\main.cpp/"));
}

TEST(Path, HasFileExtension)
{
  EXPECT_TRUE (has_file_extension("hi.png", "png"));
  EXPECT_FALSE(has_file_extension("hi.png", "pngg"));
  EXPECT_FALSE(has_file_extension("hi.png", "ppng"));
  EXPECT_TRUE (has_file_extension("hi.jpeg", "jpg,jpeg"));
  EXPECT_TRUE (has_file_extension("hi.jpg", "jpg,jpeg"));
  EXPECT_FALSE(has_file_extension("hi.ase", "jpg,jpeg"));
  EXPECT_TRUE (has_file_extension("hi.ase", "jpg,jpeg,ase"));
  EXPECT_TRUE (has_file_extension("hi.ase", "ase,jpg,jpeg"));
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
