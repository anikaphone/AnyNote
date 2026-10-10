#include "storage/VaultManager.h"
#include "storage/ConfigStore.h"
#include "common/StringUtils.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace anynote::storage;
void Check(bool ok,const char* error) { if(!ok) throw std::runtime_error(error); }
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    Check(input.good(),"read fixture");
    return {std::istreambuf_iterator<char>(input),{}};
}
void Write(const std::filesystem::path& path,const std::string& value) {
    std::ofstream output(path,std::ios::binary|std::ios::trunc); output<<value;
    Check(output.good(),"write fixture");
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==2,"scratch path required");
        std::filesystem::path dir(argv[1]); Check(std::filesystem::create_directory(dir),"scratch exists");
        const auto ini=dir/L"anynote.ini", json=dir/L"anynote.json";
        Write(ini,std::string("\xff\xfe",2));
        auto set=[&](const wchar_t* section,const wchar_t* key,const std::wstring& value) {
            Check(WritePrivateProfileStringW(section,key,value.c_str(),ini.c_str())!=FALSE,"INI write");
        };
        const auto external=(dir.parent_path()/L"external-笔记"/L"same.anynote").wstring();
        set(L"General",L"ActiveVaultPath",external);
        set(L"Vaults",L"Count",L"2");
        set(L"Vaults",L"Vault_0_Name",L"本地库"); set(L"Vaults",L"Vault_0_Path",L"notebooks/same.anynote");
        set(L"Vaults",L"Vault_1_Name",L"外部库"); set(L"Vaults",L"Vault_1_Path",external);
        set(L"View",L"OutlineWidth",L"333"); set(L"Window",L"AlwaysOnTop",L"1");
        set(L"Editor",L"LastCodeLanguage",L"Cpp"); set(L"UnknownSection",L"未知键",std::wstring(2048,L'中'));
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,ini.c_str());
        const auto originalIni=Bytes(ini);
        VaultManager manager(ini.wstring());
        Check(manager.Load(),manager.GetLastError().c_str());
        Check(Bytes(ini)==originalIni,"INI modified by migration");
        Check(manager.GetVaultCount()==2 && manager.GetActiveVaultPath()==external,"order/active path lost");
        Check(manager.GetVault(0)->name==L"本地库" && manager.GetVault(1)->name==L"外部库","names lost");
        Check(manager.GetConfigString(L"view",L"outlinewidth")==L"333","case insensitive INI setting");
        Check(manager.GetConfigString(L"UnknownSection",L"未知键")==std::wstring(2048,L'中'),"unknown/long setting truncated");
        const std::string uid="0123456789abcdef";
        const auto local=(dir/L"notebooks"/L"same.anynote").wstring();
        Check(manager.RegisterVaultIdentity(local,uid),"register UID");
        Check(ConfigStore::Read(json.wstring()).value.at("vault_files").at(uid).get<std::string>()=="same.anynote","UID filename map");
        Check(manager.RegisterVaultIdentity(external,uid),"register duplicate copy");
        Check(manager.FindVaultsByUid(uid).size()==2,"duplicate UID not detected");
        Check(!ConfigStore::Read(json.wstring()).value.at("vault_files").contains(uid),"ambiguous UID silently selected");
        auto state=ConfigStore::Read(json.wstring());
        state.value["future_setting"]={{"keep",true}};
        state.value["vaults"][0]["extra"]="retained";
        ConfigStore::Write(json.wstring(),state.value,state.fingerprint);
        Check(manager.Load() && manager.SetConfigString(L"Editor",L"LastCodeLanguage",L"Python"),"JSON reload/write");
        auto updated=ConfigStore::Read(json.wstring()).value;
        Check(updated.at("future_setting").at("keep").get<bool>() && updated["vaults"][0]["extra"]=="retained","unknown JSON field lost");
        VaultManager other(ini.wstring()); Check(other.Load(),"second instance load");
        Check(other.SetConfigString(L"View",L"OutlineWidth",L"444"),"second instance write");
        Check(!manager.RenameVault(0,L"stale write"),"stale overwrite accepted");
        Check(manager.GetVault(0)->name==L"本地库","failed save did not restore memory");
        Check(manager.Load(),"refresh after conflict");
        Check(manager.GetConfigString(L"View",L"OutlineWidth")==L"444","external change lost");
        VaultManager readWithoutLoad(ini.wstring());
        Check(readWithoutLoad.GetConfigString(L"Editor",L"LastCodeLanguage")==L"Python","legacy getter behavior");
        const auto beforeFailure=Bytes(json);
        const auto backup=std::filesystem::path(json.wstring()+L".bak");
        std::filesystem::rename(backup,backup.wstring()+L".saved");
        Check(std::filesystem::create_directory(backup),"backup failure fixture");
        Check(!manager.SetConfigString(L"View",L"OutlineWidth",L"555"),"backup failure accepted");
        Check(Bytes(json)==beforeFailure && manager.GetConfigString(L"View",L"OutlineWidth")==L"444","failed atomic save changed state");
        Write(json,"{broken JSON");
        VaultManager corrupt(ini.wstring());
        Check(!corrupt.Load() && !corrupt.Save(),"corrupt JSON fell back to stale INI");
        Check(Bytes(json)=="{broken JSON" && Bytes(ini)==originalIni,"corrupt configuration overwritten");
        Write(json,"{\"schema_version\":1,\"schema_version\":1}");
        VaultManager duplicate(ini.wstring()); Check(!duplicate.Load(),"duplicate JSON key accepted");
        std::cout<<"INI/JSON migration, unknown settings, external paths, UID copies, atomic failure and concurrent update checks passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
