Interceptor.attach(Module.getExportByName('kernelbase.dll', 'DeriveCapabilitySidsFromName'), {
  onLeave(retval) {
    retval.replace(0);
  }
});

Interceptor.attach(Module.getExportByName('kernel32.dll', 'DeviceIoControl'), {
  onEnter(args) {
    this.shit = null;
    if (args[1].toInt32() === 3080195)
    {
      if (args[2].add(16).readUInt() == 4)
        this.shit = args[4];
      //console.log(args[1].toInt32());
      /*console.log(hexdump(args[2], {
        length: 4,
        ansi: true
      }))*/
      //console.log(args[2].readByteArray(4).unwrap().toInt32());
    }
  },
  onLeave(retval) {
    if (this.shit) {
      console.log(hexdump(this.shit));
    }
  }
});

const CloseHandle = new NativeFunction(Module.findExportByName("kernelbase.dll", "CloseHandle"), 'long', ['pointer']);
Interceptor.attach(Module.getExportByName('kernelbase.dll', 'CreateFileW'), {
  onEnter(args) {
    this.block = !args[0].readUtf16String().includes("REARLINEOUTWAVE3");
    if (0)
    {
      console.log(args[0].readUtf16String());
      console.log(args[1].toInt32());
      console.log(args[2].toInt32());
      console.log(args[4].toInt32());
      console.log(args[5].toInt32());
    }
  },
  onLeave(retval) {
    if (this.block) {
      CloseHandle(retval);
      retval.replace(-1);
    }
  }
});

const mods = ["audioses", "rtkcfg64", "mmdevapi"];
const FreeLibrary = new NativeFunction(Module.findExportByName("kernelbase.dll", "FreeLibrary"), 'long', ['pointer']);
Interceptor.attach(Module.getExportByName('kernelbase.dll', 'LoadLibraryExW'), {
  onEnter(args) {
    const lpLibFileName = args[0].readUtf16String().toLowerCase();
    this.block = mods.some(el => lpLibFileName.includes(el));
  },
  onLeave(retval) {
    if (this.block) {
      FreeLibrary(retval);
      retval.replace(NULL);
    }
  }
});

// const ghidraImageBase = 0x140000000; // example value get the real value in Ghidra from Window -> Memory map -> Set Image Base
// const moduleName = "RtkAudUService64.exe";
// const moduleBaseAddress = Module.findBaseAddress(moduleName);
// const functionRealAddress = moduleBaseAddress.add(0x140027fd0 - ghidraImageBase);
// Interceptor.attach(functionRealAddress, {
//   onEnter: function(args) {
//     const x = args[0].readUtf16String();
//     if (x === "%s %d nDriverEventIndex=%d")
//     {
//       console.log(args[1].readUtf16String());
//       console.log(args[2].toInt32());
//       console.log(args[3].toInt32());
//     }
//   }
// });
