#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
int main(int argc,char**argv){
 if(argc!=2){std::cerr<<"usage: recover_weapon_runtime OUT_DIR\n";return 2;}
 fs::path out=argv[1];fs::create_directories(out);

 std::ofstream a(out/"weapon_scheduler_corrected.csv");
 a<<"name,address,role,confidence\n";
 a<<"weapon_post_tick,0x003390A0,scheduler wrapper calling projectile-manager post tick,high\n";
 a<<"weapon_pre_tick,0x003390E0,scheduler wrapper calling projectile-manager pre tick,high\n";
 a<<"projectile_manager_post_tick,0x00334F90,walks projectile objects via vtable +0x0C,high\n";
 a<<"projectile_manager_pre_tick,0x00335110,walks projectile objects via vtable +0x08,high\n";

 std::ofstream b(out/"weapon_vtables.csv");
 b<<"class,typeinfo,vtable,slot,address,role,confidence\n";
 b<<"CZProjectile,0x00470A50,0x0048A9B0,+0x08,0x00333540,pre/update virtual,high\n";
 b<<"CZProjectile,0x00470A50,0x0048A9B0,+0x0C,0x00333070,post/update virtual,high\n";
 b<<"CZWeapon,0x00470EF0,0x0048A9C0,+0x08,0x0033A1C0,destructor/teardown virtual,high\n";
 b<<"CZWeapon,0x00470EF0,0x0048A9C0,+0x0C,0x00339E30,weapon action/projectile-spawn virtual,medium-high\n";

 std::ofstream c(out/"weapon_definition_layout.csv");
 c<<"offset,name,evidence,confidence\n";
 c<<"+0x48,FireWait,\"literal FireWait parsed and stored here\",high\n";
 c<<"+0x1D4,Type,\"literal Type parsed and stored as byte; used by SEAL equipment category selection\",high\n";
 c<<"+0x38,weapon float field,\"retail getter 0x00339800\",high\n";
 c<<"+0x3C,weapon float field,\"retail getter 0x003397F0\",high\n";
 c<<"+0x40,weapon float field,\"retail getter 0x003397D0\",high\n";
 c<<"+0x44,weapon float field,\"retail getter 0x003397E0\",high\n";
 c<<"n/a,NumZoomModes,\"parsed by weapon-definition loader around 0x00336C68\",high\n";
 c<<"n/a,ZoomMode%d,\"loop-parsed and forwarded to weapon zoom-mode setup\",high\n";
 c<<"n/a,AccBurstCnt_Min/Max,\"parsed and forwarded to accuracy helpers\",high\n";
 c<<"n/a,AccScalar_Min/Max,\"parsed and forwarded to accuracy helpers\",high\n";

 std::ofstream d(out/"weapon_cross_system.txt");
 d <<
R"(SCUS_971.34 weapon/runtime recovery

Corrected scheduler callbacks:
  weapon_post_tick = 0x003390A0
  weapon_pre_tick  = 0x003390E0

The earlier 0x003490A0 / 0x003490E0 labels were wrong by 0x10000 and landed inside another function.

Projectile manager:
  global pointer = 0x0048E568 (GP - 0x6108)
  disable flag   = 0x0048E574 (GP - 0x60FC)

weapon_pre_tick:
  0x003390E0 -> 0x00335110
  manager iterates active projectile objects and invokes vtable +0x08.

weapon_post_tick:
  0x003390A0 -> 0x00334F90
  manager iterates active projectile objects and invokes vtable +0x0C.
  completed objects are detached/retired from the manager.

CZProjectile:
  type info = 0x00470A50
  vtable    = 0x0048A9B0
  +0x08 -> 0x00333540
  +0x0C -> 0x00333070

CZWeapon:
  type info = 0x00470EF0
  vtable    = 0x0048A9C0
  +0x08 -> 0x0033A1C0 (destruction/teardown)
  +0x0C -> 0x00339E30 (weapon action; initializes/spawns projectile-side state)

The weapon action at 0x00339E30 calls into the projectile construction/setup path at 0x00333E70 and interacts with the global projectile manager, making it the current strongest candidate for the retail fire/projectile-spawn virtual.

Weapon-definition parser:
  FireWait       -> +0x48
  Type           -> +0x1D4
  NumZoomModes   -> parsed
  ZoomMode%d     -> parsed in loop
  AccBurstCnt_Min/Max -> parsed
  AccScalar_Min/Max   -> parsed

SEAL equipment:
  current weapon comes from CSeal+0x530 equipment runtime.
  weapon Type (+0x1D4) is used by the SEAL creation/equipment path for rifle/pistol/grenade categorization.
)";
 std::cout<<"Weapon runtime reports written to "<<out.string()<<"\n";
}
