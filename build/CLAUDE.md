# build/ — compiled output

```
libneuronmod.so   the compiled mod, as shipped in v230 (177 KB in the apk)
classes4.dex      neuron.mod.Toaster, compiled once and added to the apk
java-classes/     the intermediate .class files for Toaster
```

Everything here is regenerable from `src/`, so nothing here is precious — but
`classes4.dex` is only rebuilt when `Toaster.java` changes, which is roughly never, and
rebuilding it needs `javac` + `d8`.

`scripts/build.sh` writes `libneuronmod.so` next to the source it compiles. Point it here, or
leave it; just do not commit two copies that can drift apart.
