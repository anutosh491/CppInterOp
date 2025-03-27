#include "gtest/gtest.h"

#include "clang/Interpreter/CppInterOp.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

// This function isn't referenced outside its translation unit, but it
// can't use the "static" keyword because its address is used for
// GetMainExecutable (since some platforms don't support taking the
// address of main, and some platforms can't implement GetMainExecutable
// without being given the address of a function in the main executable).
std::string GetExecutablePath(const char* Argv0) {
  // This just needs to be some symbol in the binary; C++ doesn't
  // allow taking the address of ::main however.
  void* MainAddr = (void*)intptr_t(GetExecutablePath);
  return llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
}

TEST(DynamicLibraryManagerTest, Sanity) {
#ifdef EMSCRIPTEN
  GTEST_SKIP() << "Test fails for Emscipten builds";
#endif
  EXPECT_TRUE(Cpp::CreateInterpreter());
  EXPECT_FALSE(Cpp::GetFunctionAddress("ret_zero"));

  std::string BinaryPath = GetExecutablePath(/*Argv0=*/nullptr);
  llvm::StringRef Dir = llvm::sys::path::parent_path(BinaryPath);
  Cpp::AddSearchPath(Dir.str().c_str());

  // FIXME: dlsym on mach-o takes the C-level name, however, the macho-o format
  // adds an additional underscore (_) prefix to the lowered names. Figure out
  // how to harmonize that API.
#ifdef __APPLE__
  std::string PathToTestSharedLib =
      Cpp::SearchLibrariesForSymbol("_ret_zero", /*system_search=*/false);
#else
  std::string PathToTestSharedLib =
      Cpp::SearchLibrariesForSymbol("ret_zero", /*system_search=*/false);
#endif // __APPLE__

  EXPECT_STRNE("", PathToTestSharedLib.c_str())
      << "Cannot find: '" << PathToTestSharedLib << "' in '" << Dir.str()
      << "'";

  EXPECT_TRUE(Cpp::LoadLibrary(PathToTestSharedLib.c_str()));
  // Force ExecutionEngine to be created.
  Cpp::Process("");
  // FIXME: Conda returns false to run this code on osx.
#ifndef __APPLE__
  EXPECT_TRUE(Cpp::GetFunctionAddress("ret_zero"));
#endif //__APPLE__

  Cpp::UnloadLibrary("TestSharedLib");
  // We have no reliable way to check if it was unloaded because posix does not
  // require the library to be actually unloaded but just the handle to be
  // invalidated...
  // EXPECT_FALSE(Cpp::GetFunctionAddress("ret_zero"));
}

TEST(DynamicLibraryManagerTest, BasicSymbolLookup) {
#ifndef EMSCRIPTEN
  GTEST_SKIP() << "This test is only intended for Emscripten builds.";
#endif

  // 1. Create interpreter
  ASSERT_TRUE(Cpp::CreateInterpreter());

  // 2. Before loading, the symbol should not exist
  EXPECT_FALSE(Cpp::GetFunctionAddress("ret_zero"));

  // 3. Load the library manually. Use exact known preload path (MEMFS path)
  const char* wasmLibPath = "libTestSharedLib.so";  // Preloaded path in MEMFS
  EXPECT_TRUE(Cpp::LoadLibrary(wasmLibPath, false));

  // 4. Force engine setup (optional here)
  Cpp::Process("");

  // 5. Symbol should now be found
  void* Addr = Cpp::GetFunctionAddress("ret_zero");
  EXPECT_NE(Addr, nullptr) << "Symbol 'ret_zero' not found after dlopen.";

  // 6. Optionally, cast and call to test actual function (should return 0)
  using RetZeroFn = int (*)();
  RetZeroFn Fn = reinterpret_cast<RetZeroFn>(Addr);
  EXPECT_EQ(Fn(), 0);
}
