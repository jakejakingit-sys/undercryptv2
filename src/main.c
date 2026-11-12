#include <graphx.h>
#include <keypadc.h>
#include <sys/timers.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#ifdef __TICE__
#include <fileioc.h>
#endif

#define SW 320
#define SH 240
#define FPS_TICKS 546
#define MAX_BULLETS 32
#define MAX_SHOTS 6
#define MAX_PARTS 40
#define ROOM_W 20
#define ROOM_H 13
#define Q 4

enum { C_VOID, C_BG, C_FLOOR, C_FLOOR2, C_WALL, C_WALLHI, C_RUNE, C_RUNE_D,
       C_RED, C_ORANGE, C_GOLD, C_WHITE, C_MINT, C_BLUE, C_PURPLE, C_GREEN };
enum { ST_MENU, ST_MODE, ST_STATS, ST_CONTROLS, ST_EXPLORE, ST_BOSS, ST_WIN, ST_LOSE, ST_PAUSE };
typedef struct { int x,y,vx,vy; uint8_t life,on,kind; } Shot;
typedef struct { int x,y,vx,vy; uint8_t life,on,kind; } Bullet;
typedef struct { int x,y,vx,vy,life,col; } Particle;
typedef struct { int x,y,w,h; } Block;

static uint16_t rng=0x91E7;
static uint32_t ticks;
static int state=ST_MENU, floor_no, room_no, hp=6, hp_max=6, boss_hp=36, boss_max=36;
static int px=45,py=120, face=1, fire_cd,dash_cd,dash_time,echo_time,echo_x,echo_y;
static int iframes,shot_level,score,room_seed,block_n,enemy_x,enemy_y,enemy_hp;
static int enemy_kind,enemy_clock,enemy_shot_cd,room_layout,hazard_clock;
static int boss_id,boss_clock,boss_warn_x,boss_warn_y,boss_phase,door_open,boss_pattern;
static int paused_state=ST_EXPLORE;
static int menu_sel,mode_sel,run_mode,run_rooms,run_kills,run_bosses,run_dodges;
static bool prev_dash,prev_enter,prev_clear,prev_up,prev_down,enemy_alive,heal_present,recorded_run,quit_game;
typedef struct { uint16_t magic; uint32_t runs,rooms,kills,bosses,dodges,best_score; uint16_t best_floor; } Records;
static Records records;
static Block blocks[8]; static Shot shots[MAX_SHOTS]; static Bullet bullets[MAX_BULLETS]; static Particle particles[MAX_PARTS];
static const int DIR_X[16]={16,15,11,6,0,-6,-11,-15,-16,-15,-11,-6,0,6,11,15};
static const int DIR_Y[16]={0,6,11,15,16,15,11,6,0,-6,-11,-15,-16,-15,-11,-6};
static const char *const BOSS_NAMES[3]={"THE BONE REGENT","THE GLASS WIDOW","THE HOLLOW ENGINE"};

static uint16_t rnd(void){ rng^=rng<<7; rng^=rng>>9; rng^=rng<<8; return rng; }
static int ri(int n){return n?(int)(rnd()%(uint16_t)n):0;}
static int absi(int n){return n<0?-n:n;}
static int clampi(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static void color(int c){gfx_SetColor((uint8_t)c);}
static void set_theme(int theme){
  static const uint8_t base[16][3]={{0,0,0},{8,8,18},{22,24,38},{32,38,56},{58,64,78},{106,112,122},{180,164,130},{104,50,68},{235,54,58},{246,132,54},{255,204,105},{255,244,213},{80,242,220},{63,152,201},{112,74,168},{86,207,132}};
  static const uint8_t tones[3][11][3]={
    {{8,8,18},{22,24,38},{32,38,56},{58,64,78},{180,164,130},{180,164,130},{104,50,68},{80,242,220},{63,152,201},{112,74,168},{86,207,132}},
    {{3,15,23},{10,34,48},{18,57,71},{34,62,78},{111,163,174},{91,218,234},{38,128,163},{113,246,232},{54,151,199},{82,116,192},{78,207,170}},
    {{18,4,22},{44,11,37},{73,17,50},{79,36,58},{167,103,106},{255,160,93},{156,61,93},{255,214,114},{212,91,76},{164,75,155},{178,177,111}}
  };
  static const uint8_t idx[11]={C_BG,C_FLOOR,C_FLOOR2,C_WALL,C_WALLHI,C_RUNE,C_RUNE_D,C_MINT,C_BLUE,C_PURPLE,C_GREEN};
  uint16_t pal[16];
  for(int i=0;i<16;i++)pal[i]=gfx_RGBTo1555(base[i][0],base[i][1],base[i][2]);
  for(int i=0;i<11;i++){int k=idx[i];pal[k]=gfx_RGBTo1555(tones[theme%3][i][0],tones[theme%3][i][1],tones[theme%3][i][2]);}
  gfx_SetPalette(pal,sizeof(pal),0);
}
static void show_menu(void){set_theme(0);state=ST_MENU;}
static void box(int x,int y,int w,int h,int c){color(c);gfx_FillRectangle(x,y,w,h);}
static void line(int x,int y,int x2,int y2,int c){color(c);gfx_Line(x,y,x2,y2);}
static void txt(const char*s,int x,int y,int c){gfx_SetTextFGColor((uint8_t)c);gfx_PrintStringXY(s,x,y);}
static void center(const char*s,int y,int c){int n=0;while(s[n])n++;txt(s,160-n*4,y,c);}
static void number(int n,int x,int y,int c){gfx_SetTextFGColor((uint8_t)c);gfx_SetTextXY(x,y);gfx_PrintUInt((unsigned)n,1);}
static void records_load(void){
  memset(&records,0,sizeof(records));
#ifdef __TICE__
  uint8_t h=ti_Open("UCRSTAT","r");
  if(h){Records r;if(ti_Read(&r,sizeof(r),1,h)==1&&r.magic==0x5543)records=r;ti_Close(h);}
#endif
  if(records.magic!=0x5543){memset(&records,0,sizeof(records));records.magic=0x5543;}
}
static void records_save(void){
#ifdef __TICE__
  uint8_t h=ti_Open("UCRSTAT","w");
  if(h){ti_Write(&records,sizeof(records),1,h);ti_SetArchiveStatus(true,h);ti_Close(h);}
#endif
}
static void finish_run(void){
  if(recorded_run)return;recorded_run=true;records.runs++;
  records.rooms+=(uint32_t)run_rooms;records.kills+=(uint32_t)run_kills;records.bosses+=(uint32_t)run_bosses;records.dodges+=(uint32_t)run_dodges;
  if((uint32_t)score>records.best_score)records.best_score=(uint32_t)score;
  if((uint16_t)(floor_no+1)>records.best_floor)records.best_floor=(uint16_t)(floor_no+1);
  records_save();
}
static bool overlap(int ax,int ay,int aw,int ah,int bx,int by,int bw,int bh){return ax<bx+bw&&ax+aw>bx&&ay<by+bh&&ay+ah>by;}
static void sparkle(int x,int y,int col,int count){for(int i=0;i<count;i++)for(int j=0;j<MAX_PARTS;j++)if(!particles[j].life){particles[j]=(Particle){x,y,(ri(9)-4)*Q,(ri(9)-6)*Q,14+ri(22),col};break;}}
static void add_bullet(int x,int y,int tx,int ty,int speed,int kind){
  int dx=tx-x,dy=ty-y,m=absi(dx)>absi(dy)?absi(dx):absi(dy);if(!m)m=1;
  for(int i=0;i<MAX_BULLETS;i++)if(!bullets[i].on){bullets[i]=(Bullet){x*Q,y*Q,dx*speed*Q/m,dy*speed*Q/m,150,1,(uint8_t)kind};break;}
}
static void add_shot(int x,int y,int tx,int ty){
  int dx=tx-x,dy=ty-y,m=absi(dx)>absi(dy)?absi(dx):absi(dy);if(!m)m=1;
  for(int i=0;i<MAX_SHOTS;i++)if(!shots[i].on){shots[i]=(Shot){x*Q,y*Q,dx*6*Q/m,dy*6*Q/m,70,1,0};break;}
}
static bool blocked(int x,int y){
  if(x<14||x>306||y<35||y>229)return true;
  for(int i=0;i<block_n;i++)if(overlap(x-4,y-5,9,11,blocks[i].x,blocks[i].y,blocks[i].w,blocks[i].h))return true;
  return false;
}
static bool projectile_blocked(int x,int y){for(int i=0;i<block_n;i++)if(overlap(x-1,y-1,3,3,blocks[i].x,blocks[i].y,blocks[i].w,blocks[i].h))return true;return false;}
static void make_room(void){
  static const Block layouts[4][5]={
    {{72,68,18,24},{226,68,18,24},{72,166,18,24},{226,166,18,24},{0,0,0,0}},
    {{94,54,18,42},{208,144,18,42},{143,94,30,18},{0,0,0,0},{0,0,0,0}},
    {{58,72,26,18},{235,72,26,18},{58,165,26,18},{235,165,26,18},{0,0,0,0}},
    {{98,58,20,22},{202,58,20,22},{98,170,20,22},{202,170,20,22},{0,0,0,0}}
  };
  px=30;py=132;room_seed=(int)rnd();rng^=(uint16_t)(floor_no*193+room_no*71+room_seed);
  room_layout=(room_no+floor_no)%4;block_n=0;heal_present=(ri(4)==0);door_open=false;enemy_alive=false;enemy_hp=0;hazard_clock=0;boss_warn_y=66+ri(8)*20;enemy_clock=0;enemy_shot_cd=55+ri(45);
  memset(shots,0,sizeof(shots));memset(bullets,0,sizeof(bullets));
  for(int i=0;i<5;i++)if(layouts[room_layout][i].w)blocks[block_n++]=layouts[room_layout][i];
  if(room_no<4){enemy_alive=true;enemy_kind=(room_no+floor_no)%3;enemy_hp=5+floor_no/2;enemy_x=218+ri(48);enemy_y=70+ri(115);}
  face=1;
}
static void start_run(void){
  uint16_t seed=(uint16_t)(timer_Get(1)^timer_Get(2)^ticks^0xB17D);rng=seed?seed:0xA531;
  floor_no=0;room_no=0;hp=6;hp_max=6;shot_level=0;score=0;dash_cd=echo_time=fire_cd=iframes=0;
  run_rooms=run_kills=run_bosses=run_dodges=0;recorded_run=false;
  memset(particles,0,sizeof(particles));set_theme(0);make_room();state=ST_EXPLORE;
}
static void start_boss(void){
  state=ST_BOSS;boss_id=floor_no%3;boss_max=36+floor_no*6;boss_hp=boss_max;
  boss_clock=0;boss_phase=0;boss_pattern=0;boss_warn_x=px;boss_warn_y=py;door_open=false;
  px=42;py=132;block_n=0;room_seed=(int)rnd();
  memset(bullets,0,sizeof(bullets));memset(shots,0,sizeof(shots));
}
static void next_room(void){
  run_rooms++;
  if(room_no==4){
    floor_no++;if(run_mode==0&&floor_no>=3){state=ST_WIN;finish_run();return;}
    room_no=0;hp=clampi(hp+1,0,hp_max);shot_level++;set_theme(floor_no);
  }else room_no++;
  if(room_no==4)start_boss();else{make_room();state=ST_EXPLORE;}
}
static int target_x(void){return echo_time?echo_x:px;}
static int target_y(void){return echo_time?echo_y:py;}
static void move_player(int dx,int dy){
  if(!dx&&!dy)return;face=dx?dx:face;int speed=dash_time?7:3;if(dx&&dy)speed=dash_time?5:2;
  int nx=px+dx*speed,ny=py+dy*speed;if(!blocked(nx,py))px=nx;if(!blocked(px,ny))py=ny;
}
static void spawn_aimed_fan(int n,int spread){
  int tx=boss_warn_x,ty=boss_warn_y;
  int sp=3+boss_pattern+floor_no/4;if(sp>6)sp=6;
  for(int k=0;k<n;k++){int off=(k-(n-1)/2)*spread;add_bullet(160,54,tx+off,ty,sp,1);}
}
static void spawn_ring(int cx,int cy,int count,int gap){
  int rot=(int)(ticks/130)%16;
  for(int i=0;i<count;i++){
    int a=(i*16/count+rot)&15;
    if(gap&&((a-rot+16)&15)<gap)continue;
    int tx=cx+DIR_X[a]*5,ty=cy+DIR_Y[a]*5;
    int sp=3+boss_pattern+floor_no/4;if(sp>6)sp=6;add_bullet(cx,cy,tx,ty,sp,2);
  }
}
static void boss_update(void){
  if(door_open)return;
  boss_clock++;
  boss_pattern=boss_hp>boss_max*2/3?0:boss_hp>boss_max/3?1:2;
  int period=boss_id==0?190:boss_id==1?180:180;
  if(boss_pattern==1)period-=22;
  if(boss_pattern==2)period-=42;
  int t=boss_clock%period;
  if(boss_id==0){
    if(t==1||t==78){boss_warn_x=target_x();boss_warn_y=target_y();}
    if(t==35)spawn_aimed_fan(5+boss_pattern*2,28-boss_pattern*3);
    if(t==112)spawn_aimed_fan(3+boss_pattern*2,24);
    if(boss_pattern==2&&t==138)spawn_ring(160,80,12,3);
  }else if(boss_id==1){
    if(t==1||t==70){boss_warn_x=target_x();boss_warn_y=target_y();}
    if(t==30)boss_phase=1;
    if(t==49){boss_phase=0;spawn_aimed_fan(3+boss_pattern*2,25);}
    if(t==86)boss_phase=1;
    if(t==105)boss_phase=0;
    if(t==120)spawn_aimed_fan(3+boss_pattern*2,25);
    if(t==period-1)boss_phase=0;
  }else{
    if(t==1||t==84){boss_warn_x=target_x();boss_warn_y=target_y();}
    if(t==36)spawn_ring(160,86,12+boss_pattern*2,4);
    if(t==72&&boss_pattern>0)spawn_aimed_fan(3+boss_pattern*2,24);
    if(t==119&&boss_pattern==2)spawn_ring(160,86,16,4);
  }
  for(int i=0;i<MAX_BULLETS;i++)if(bullets[i].on){Bullet*b=&bullets[i];b->x+=b->vx;b->y+=b->vy;if(b->life)b->life--;if(!b->life||b->x<-12*Q||b->x>332*Q||b->y<-12*Q||b->y>252*Q||projectile_blocked(b->x/Q,b->y/Q))b->on=0;
    if(b->on&&!iframes&&absi(b->x/Q-px)<8&&absi(b->y/Q-py)<8){b->on=0;hp--;iframes=46;sparkle(px,py,C_RED,9);if(hp<=0){state=ST_LOSE;finish_run();}}}
  if(boss_id==1&&boss_phase&&!iframes&&absi(px-boss_warn_x)<9){hp--;iframes=46;sparkle(px,py,C_RED,9);if(hp<=0){state=ST_LOSE;finish_run();}}
  for(int i=0;i<MAX_SHOTS;i++)if(shots[i].on){Shot*s=&shots[i];s->x+=s->vx;s->y+=s->vy;if(s->life)s->life--;if(!s->life||s->x<-10*Q||s->x>330*Q||s->y<-10*Q||s->y>250*Q)s->on=0;
    if(s->on&&projectile_blocked(s->x/Q,s->y/Q))s->on=0;
    if(s->on&&!door_open&&absi(s->x/Q-160)<28&&absi(s->y/Q-61)<32){s->on=0;boss_hp--;score+=10;sparkle(160,72,C_GOLD,5);if(boss_hp<=0){boss_hp=0;door_open=true;boss_phase=0;boss_clock=0;run_bosses++;score+=250;memset(bullets,0,sizeof(bullets));memset(shots,0,sizeof(shots));}}
  }
}
static void explore_update(int left,int right,int up,int down,bool fire){
  int dx=right-left,dy=down-up;move_player(dx,dy);enemy_clock++;hazard_clock++;
  if(fire&&fire_cd==0){
    if(state==ST_EXPLORE&&enemy_alive)add_shot(px,py,enemy_x,enemy_y);
    fire_cd=shot_level?7:10;
  }
  if(state==ST_EXPLORE&&enemy_alive){
    int tx=target_x(),ty=target_y();
    if(enemy_kind==0||enemy_kind==2){int sp=enemy_kind==2&&enemy_clock%90<12?3+floor_no/5:1+floor_no/7;if(sp>5)sp=5;if(enemy_x<tx-2)enemy_x+=sp;else if(enemy_x>tx+2)enemy_x-=sp;if(enemy_y<ty-2)enemy_y+=sp;else if(enemy_y>ty+2)enemy_y-=sp;}
    else {if(enemy_x<252)enemy_x++;if(enemy_shot_cd>0)enemy_shot_cd--;else{int sp=3+floor_no/3;if(sp>6)sp=6;add_bullet(enemy_x,enemy_y,px,py,sp,1);enemy_shot_cd=96-floor_no*4;if(enemy_shot_cd<42)enemy_shot_cd=42;}}
    if(enemy_kind==2&&enemy_clock%90==1){enemy_x=clampi(px+(ri(2)?-1:1)*(70+ri(45)),70,258);enemy_y=clampi(py+(ri(2)?-1:1)*(42+ri(40)),56,218);sparkle(enemy_x,enemy_y,C_PURPLE,8);}
    if(!iframes&&absi(px-enemy_x)<11&&absi(py-enemy_y)<11){hp--;iframes=48;sparkle(px,py,C_RED,8);if(hp<=0){state=ST_LOSE;finish_run();}}
    for(int i=0;i<MAX_SHOTS;i++)if(shots[i].on){Shot*s=&shots[i];s->x+=s->vx;s->y+=s->vy;if(s->life)s->life--;if(!s->life||projectile_blocked(s->x/Q,s->y/Q))s->on=0;if(s->on&&absi(s->x/Q-enemy_x)<10&&absi(s->y/Q-enemy_y)<10){s->on=0;enemy_hp--;sparkle(enemy_x,enemy_y,C_GOLD,4);if(enemy_hp<=0){enemy_alive=false;door_open=true;score+=80;run_kills++;}}}
  }
  for(int i=0;i<MAX_BULLETS;i++)if(bullets[i].on){Bullet*b=&bullets[i];b->x+=b->vx;b->y+=b->vy;if(b->life)b->life--;if(!b->life||b->x<-12*Q||b->x>332*Q||b->y<-12*Q||b->y>252*Q||projectile_blocked(b->x/Q,b->y/Q))b->on=0;if(b->on&&!iframes&&absi(b->x/Q-px)<8&&absi(b->y/Q-py)<8){b->on=0;hp--;iframes=42;sparkle(px,py,C_RED,8);if(hp<=0){state=ST_LOSE;finish_run();}}}
  if(room_layout==2&&room_no<4){int ht=hazard_clock%190;if(ht==130)boss_warn_y=66+ri(8)*20;if(ht>=162&&ht<184&&!iframes&&absi(py-boss_warn_y)<8){hp--;iframes=44;sparkle(px,py,C_RED,8);if(hp<=0){state=ST_LOSE;finish_run();}}}
  if(heal_present&&absi(px-158)<10&&absi(py-112)<10){heal_present=false;hp=clampi(hp+1,0,hp_max);score+=25;sparkle(158,112,C_MINT,12);}
  if(door_open&&px>298&&py>102&&py<154){next_room();return;}
}
static void draw_backdrop(void){
  gfx_FillScreen(C_BG);
  for(int y=28;y<236;y+=16)for(int x=4;x<316;x+=16){int v=((x/16*13+y/16*7+room_seed)&7);box(x,y,15,15,(v==0)?C_FLOOR2:C_FLOOR);if(v==0){box(x+3,y+5,2,1,C_WALLHI);box(x+10,y+11,1,2,C_WALL);}else if(((x+y+room_seed)&31)==0){box(x+6,y+7,4,1,C_WALL);box(x+8,y+7,1,4,C_WALL);}}
  box(0,26,320,5,C_VOID);box(0,234,320,6,C_VOID);box(0,26,5,214,C_VOID);box(315,26,5,214,C_VOID);
  box(5,31,310,2,C_WALLHI);box(5,232,310,2,C_WALLHI);box(5,31,2,201,C_WALLHI);box(313,31,2,201,C_WALLHI);
  for(int x=14;x<310;x+=28){box(x,29,5,4,C_WALL);box(x+1,231,5,4,C_WALL);}
  for(int i=0;i<block_n;i++){Block*b=&blocks[i];box(b->x+2,b->y+4,b->w,b->h,C_VOID);box(b->x,b->y,b->w,b->h,C_WALL);box(b->x+2,b->y+2,b->w-4,3,C_WALLHI);box(b->x+4,b->y+b->h-4,b->w-8,2,C_BG);box(b->x+3,b->y+5,2,2,C_RUNE_D);}
  /* Region seal set into the north wall, a landmark that changes by floor. */
  box(151,35,18,3,C_WALLHI);box(154,37,12,9,C_VOID);box(157,38,6,6,C_RUNE);box(159,39,2,4,C_GOLD);
  if(room_layout==2&&room_no<4){int ht=hazard_clock%190;int hy=boss_warn_y;if(ht>=132&&ht<162){for(int x=18;x<302;x+=12)box(x,hy-2,7,4,C_RED);box(151,hy-6,18,12,C_WHITE);}else if(ht>=162&&ht<184){box(15,hy-3,290,6,C_RED);box(15,hy-1,290,2,C_WHITE);}}
  if(door_open){box(302,104,15,50,C_GOLD);box(306,108,12,42,C_VOID);box(309,114,6,30,C_RUNE_D);}
  else {box(305,104,12,50,C_WALL);box(308,108,9,42,C_BG);}
  if(heal_present){box(155,108,6,9,C_MINT);box(152,111,12,4,C_WHITE);box(157,105,2,3,C_GOLD);}
}
static void draw_player(void){
  if(iframes&&(ticks&3)==0)return;
  if(echo_time){box(echo_x-7,echo_y-8,14,17,C_BLUE);box(echo_x-5,echo_y-6,10,13,C_BG);box(echo_x-3,echo_y-4,6,8,C_MINT);box(echo_x-1,echo_y-2,2,4,C_WHITE);}
  box(px-8,py+8,16,3,C_VOID);
  /* brass diving helmet, luminous faceplate, stitched coat, and a clear lantern core */
  box(px-7,py-7,14,13,C_VOID);box(px-6,py-8,12,2,C_GOLD);box(px-5,py-9,10,2,C_WALLHI);
  box(px-6,py-6,12,10,C_WALLHI);box(px-4,py-5,8,8,C_BLUE);box(px-3,py-4,6,6,C_MINT);
  box(px-2,py-3,2,2,C_WHITE);box(px+2,py-3,2,2,C_WHITE);box(px-1,py+1,3,1,C_GOLD);
  box(px-8,py-2,3,7,C_GOLD);box(px+5,py-2,3,7,C_GOLD);box(px-6,py+4,12,2,C_WALLHI);
  box(px-5,py+6,10,7,C_BLUE);box(px-4,py+7,8,5,C_WALL);box(px-2,py+7,4,5,C_GOLD);box(px-1,py+8,2,3,C_WHITE);
  box(px-5,py+12,4,4,C_BLUE);box(px+2,py+12,4,4,C_BLUE);box(px-6,py+15,6,2,C_WALLHI);box(px+1,py+15,6,2,C_WALLHI);
  box(px-9,py+6,3,5,C_RUNE_D);box(px+6,py+6,3,5,C_RUNE_D);
  box(px+(face>0?7:-9),py-2,2,2,C_MINT);if(dash_time)box(px-10,py+1,20,2,C_WHITE);
}
static void draw_enemy(void){
  if(!enemy_alive)return;
  int bob=(int)(ticks/5%3)-1;
  if(enemy_kind==0){
    box(enemy_x-9,enemy_y-5+bob,18,12,C_VOID);box(enemy_x-7,enemy_y-8+bob,14,11,C_PURPLE);box(enemy_x-5,enemy_y-11+bob,10,4,C_RUNE_D);
    box(enemy_x-5,enemy_y-3+bob,4,4,C_RED);box(enemy_x+2,enemy_y-3+bob,4,4,C_RED);box(enemy_x-2,enemy_y+3+bob,5,2,C_VOID);
    box(enemy_x-8,enemy_y+4+bob,4,4,C_BLUE);box(enemy_x+4,enemy_y+4+bob,4,4,C_BLUE);box(enemy_x-11,enemy_y-1+bob,3,4,C_RUNE);
  }else if(enemy_kind==1){
    box(enemy_x-10,enemy_y-8+bob,20,17,C_VOID);box(enemy_x-8,enemy_y-9+bob,16,15,C_BLUE);box(enemy_x-5,enemy_y-12+bob,10,5,C_MINT);
    box(enemy_x-5,enemy_y-4+bob,4,4,C_WHITE);box(enemy_x+2,enemy_y-4+bob,4,4,C_WHITE);box(enemy_x-2,enemy_y-3+bob,3,3,C_RED);
    box(enemy_x-13,enemy_y+1+bob,5,4,C_PURPLE);box(enemy_x+8,enemy_y+1+bob,5,4,C_PURPLE);box(enemy_x-4,enemy_y+7+bob,8,3,C_GOLD);
    if(enemy_shot_cd<22){box(enemy_x-12,enemy_y-15,24,2,C_RED);}
  }else{
    box(enemy_x-11,enemy_y-6+bob,22,14,C_VOID);box(enemy_x-8,enemy_y-10+bob,16,13,C_GREEN);box(enemy_x-5,enemy_y-13+bob,10,5,C_MINT);
    box(enemy_x-5,enemy_y-4+bob,4,4,C_WHITE);box(enemy_x+2,enemy_y-4+bob,4,4,C_WHITE);box(enemy_x-4,enemy_y-3+bob,2,2,C_BLUE);box(enemy_x+3,enemy_y-3+bob,2,2,C_BLUE);
    box(enemy_x-12,enemy_y+3+bob,5,4,C_RUNE);box(enemy_x+7,enemy_y+3+bob,5,4,C_RUNE);
  }
  box(enemy_x-10,enemy_y-17,20,3,C_VOID);box(enemy_x-9,enemy_y-16,(enemy_hp*18)/(5+floor_no/2),1,C_RED);
}
static void draw_boss(void){
  int bob=(int)(ticks/10%3)-1;
  if(boss_id==0){
    box(125,50+bob,70,39,C_VOID);box(130,47+bob,60,38,C_WALLHI);box(136,43+bob,48,33,C_WHITE);box(140,39+bob,40,7,C_GOLD);
    box(130,48+bob,8,14,C_RUNE);box(182,48+bob,8,14,C_RUNE);box(140,58+bob,13,8,C_VOID);box(167,58+bob,13,8,C_VOID);
    box(143,60+bob,6,4,C_RED);box(169,60+bob,6,4,C_RED);box(143,59+bob,2,2,C_WHITE);box(169,59+bob,2,2,C_WHITE);
    box(151,69+bob,18,5,C_WALL);box(154,77+bob,12,4,C_VOID);for(int i=0;i<4;i++)box(155+i*3,78+bob,2,6,C_WHITE);
    for(int i=0;i<4;i++){box(132+i*15,44+bob,4,7,C_GOLD);box(134+i*15,38+bob,3,8,C_WHITE);}
    box(136,83+bob,10,5,C_BLUE);box(174,83+bob,10,5,C_BLUE);box(155,88+bob,11,4,C_RUNE_D);
  }else if(boss_id==1){
    for(int i=0;i<4;i++){line(139+i*11,67+bob,121+i*18,97,C_BLUE);line(139+i*11,67+bob,121+i*18,97,C_MINT);}
    box(126,49+bob,68,39,C_VOID);box(132,48+bob,56,34,C_BLUE);box(139,43+bob,42,9,C_RUNE_D);box(144,48+bob,32,4,C_MINT);
    line(136,77+bob,121,94,C_PURPLE);line(184,77+bob,199,94,C_PURPLE);line(142,83+bob,132,97,C_MINT);line(178,83+bob,188,97,C_MINT);
    box(146,55+bob,28,25,C_PURPLE);box(150,57+bob,20,18,C_WHITE);box(155,61+bob,10,10,C_RED);box(158,62+bob,4,6,C_WHITE);
    box(134,83+bob,52,4,C_RUNE);box(140,88+bob,40,3,C_GOLD);box(124,92+bob,5,4,C_MINT);box(191,92+bob,5,4,C_MINT);
  }else{
    line(126,65+bob,116,46+bob,C_BLUE);line(194,65+bob,204,46+bob,C_BLUE);line(130,79+bob,115,94+bob,C_PURPLE);line(190,79+bob,205,94+bob,C_PURPLE);
    box(126,47+bob,68,44,C_VOID);box(131,45+bob,58,42,C_PURPLE);box(136,41+bob,48,8,C_RUNE_D);box(141,46+bob,38,4,C_GOLD);
    box(142,52+bob,36,30,C_VOID);box(146,54+bob,28,24,C_WHITE);box(151,57+bob,18,18,C_RED);box(156,60+bob,8,13,C_VOID);box(158,62+bob,4,8,C_MINT);
    for(int i=0;i<4;i++){box(132+i*17,84+bob,8,4,C_GOLD);box(135+i*17,88+bob,4,5,C_RUNE);}
    box(149,91+bob,22,4,C_BLUE);
  }
  /* core / boss health */
  box(108,100,104,8,C_VOID);box(110,102,100,4,C_RUNE_D);box(110,102,boss_max?boss_hp*100/boss_max:0,4,C_RED);
  for(int i=0;i<=10;i++)box(110+i*10,101,1,6,C_WALLHI);
}
static void draw_bullets(void){
  for(int i=0;i<MAX_BULLETS;i++)if(bullets[i].on){Bullet*b=&bullets[i];int x=b->x/Q,y=b->y/Q,c=b->kind==3?C_RED:b->kind==2?C_BLUE:C_ORANGE;
    box(x-3,y-3,7,7,c);box(x-1,y-1,3,3,C_WHITE);}
  for(int i=0;i<MAX_SHOTS;i++)if(shots[i].on){Shot*s=&shots[i];box(s->x/Q-2,s->y/Q-2,5,5,C_MINT);box(s->x/Q-1,s->y/Q-1,3,3,C_WHITE);}
}
static void draw_particles(void){for(int i=0;i<MAX_PARTS;i++)if(particles[i].life){Particle*p=&particles[i];p->x+=p->vx/Q;p->y+=p->vy/Q;p->life--;box(p->x,p->y,2,2,p->col);}}
static void draw_hud(void){
  box(0,0,320,25,C_VOID);txt("UNDERCRYPT",5,5,C_GOLD);txt("FLOOR",105,5,C_WALLHI);number(floor_no+1,145,5,C_WHITE);txt("ROOM",168,5,C_WALLHI);number(room_no+1,205,5,C_WHITE);txt("HP",239,5,C_RED);
  for(int i=0;i<hp_max;i++)box(258+i*9,6,6,8,i<hp?C_RED:C_WALL);
  txt("ARROWS MOVE",5,14,C_WALLHI);txt("2ND FIRE",98,14,C_MINT);txt("ALPHA DASH",180,14,C_BLUE);box(264,16,48,3,C_WALL);if(dash_cd<54)box(264,16,(54-dash_cd)*48/54,3,C_MINT);
}
static void draw_room(void){
  draw_backdrop();
  if(state==ST_BOSS||(state==ST_PAUSE&&paused_state==ST_BOSS)){draw_boss();
    if(!door_open){int period=(boss_id==0?190:180)-boss_pattern*22,t=boss_clock%period;
      if(boss_id==0&&((t>=1&&t<35)||(t>=78&&t<112))){int x=boss_warn_x,y=boss_warn_y;box(x-8,y-1,17,2,C_RED);box(x-1,y-8,2,17,C_RED);box(x-4,y-4,8,8,C_VOID);box(x-3,y-3,6,6,C_WHITE);box(x-1,y-1,2,2,C_RED);}
      else if(boss_id==1){if(t<30||(t>=70&&t<86)){int x=boss_warn_x;for(int y=34;y<230;y+=12)box(x-3,y,7,5,C_RED);box(x-7,226,15,4,C_WHITE);}if(boss_phase){int x=boss_warn_x;box(x-8,32,17,201,C_RED);box(x-2,32,5,201,C_WHITE);}}
      else if(boss_id==2&&((t<36)||(boss_pattern==2&&t>=84&&t<119))){int rot=(int)(ticks/130)%16;for(int i=0;i<16;i++){int a=(i+rot)&15;bool gap=i<4;line(160,86,160+DIR_X[a]*5,86+DIR_Y[a]*5,gap?C_MINT:C_RED);}}
    }
  }else if(enemy_alive){draw_enemy();}
  draw_bullets();draw_player();draw_particles();draw_hud();
  if(state==ST_BOSS){center(BOSS_NAMES[boss_id],112,C_GOLD);if(door_open)center("EXIT RIGHT  >",124,C_MINT);else if(boss_id==1&&boss_phase)center("BEAM! DASH OR MOVE!",124,C_RED);else if(boss_id==2)center("FIND THE MINT GAP",124,C_MINT);else center("RED MARKS: MOVE NOW",124,C_WALLHI);}
  if(state==ST_EXPLORE){if(enemy_alive){static const char*goal[3]={"CHASE WISP","STOP THE GUNNER","FIND THE BLINKER"};center(goal[enemy_kind],216,C_WALLHI);}else center("EXIT OPEN  >",216,C_MINT);}
}
static void menu_backdrop(void){
  gfx_FillScreen(C_VOID);for(int y=0;y<240;y+=16)for(int x=0;x<320;x+=16){box(x,y,15,15,((x/16+y/16*3)&3)?C_BG:C_FLOOR);if(((x*7+y*3)&63)==0)box(x+6,y+5,2,2,C_WALLHI);}
  for(int i=0;i<18;i++){int x=(i*67+(int)(ticks/3))%320,y=(i*41+13)%240;box(x,y,2,2,(i%5)?C_BLUE:C_MINT);}
  box(14,19,292,3,C_WALLHI);box(14,221,292,3,C_WALLHI);box(14,19,3,205,C_WALLHI);box(303,19,3,205,C_WALLHI);
}
static void draw_menu(void){
  menu_backdrop();center("UNDERCRYPT",34,C_GOLD);center("DODGE THE DARK. BREAK THE SEALS.",52,C_WALLHI);
  /* small animated lantern crest */
  box(141,66,38,3,C_GOLD);box(148,69,24,6,C_WALLHI);box(151,75,18,20,C_BLUE);box(155,79,10,12,C_MINT);box(158,82,4,6,C_GOLD);box(144,96,32,3,C_GOLD);
  box(69,111,182,94,C_VOID);box(72,114,176,88,C_WALL);box(75,117,170,82,C_BG);
  static const char*opts[4]={"PLAY","RECORDS","HOW TO PLAY","QUIT"};
  for(int i=0;i<4;i++){int y=126+i*18;if(i==menu_sel){box(90,y-2,140,15,C_WALL);box(91,y-1,3,13,C_GOLD);txt(">",101,y,C_MINT);txt(opts[i],116,y,C_WHITE);}else txt(opts[i],116,y,C_WALLHI);}
  center("ARROWS SELECT     ENTER CONFIRM",211,C_WALLHI);center("A SMALL ACTION DUNGEON FOR TI-84 CE",228,C_GOLD);
}
static void draw_mode(void){
  menu_backdrop();center("CHOOSE YOUR DESCENT",43,C_GOLD);center("HOW LONG WILL YOU SURVIVE?",61,C_WALLHI);
  box(54,78,212,112,C_VOID);box(57,81,206,106,C_WALL);
  static const char*opts[3]={"STORY  /  3 GUARDIANS","INFINITE  /  ENDLESS FLOORS","BACK"};
  for(int i=0;i<3;i++){int y=103+i*25;if(i==mode_sel){box(67,y-3,186,19,C_BG);box(69,y-1,3,15,C_GOLD);txt(">",78,y+1,C_MINT);txt(opts[i],91,y+1,C_WHITE);}else txt(opts[i],91,y+1,C_WALLHI);}
  center("INFINITE ADDS SPEED AND PATTERNS EACH FLOOR",204,C_MINT);center("ENTER TO SELECT   CLEAR TO RETURN",222,C_WALLHI);
}
static void draw_stats(void){
  menu_backdrop();center("CRYPT RECORDS",41,C_GOLD);center("ALL-TIME / SAVED ON CALCULATOR",59,C_WALLHI);
  box(42,76,236,124,C_VOID);box(45,79,230,118,C_WALL);
  txt("RUNS",61,88,C_WALLHI);number((int)records.runs,225,88,C_WHITE);
  txt("ROOMS CLEARED",61,103,C_WALLHI);number((int)records.rooms,225,103,C_MINT);
  txt("ENEMIES DEFEATED",61,118,C_WALLHI);number((int)records.kills,225,118,C_MINT);
  txt("GUARDIANS DEFEATED",61,133,C_WALLHI);number((int)records.bosses,225,133,C_GOLD);
  txt("DASHES USED",61,148,C_WALLHI);number((int)records.dodges,225,148,C_BLUE);
  txt("BEST FLOOR",61,163,C_WALLHI);number((int)records.best_floor,225,163,C_WHITE);
  txt("BEST SCORE",61,178,C_WALLHI);number((int)records.best_score,225,178,C_GOLD);
  center("CLEAR TO RETURN",219,C_WALLHI);
}
static void draw_controls(void){
  menu_backdrop();center("FIELD GUIDE",43,C_GOLD);center("ARROWS  MOVE THROUGH THE ROOM",78,C_WHITE);center("2ND     FIRE A LANTERN BOLT",101,C_MINT);center("ALPHA   DASH + LEAVE A DECOY",124,C_BLUE);center("DASH THROUGH ATTACKS TO AVOID DAMAGE",153,C_WALLHI);center("RED MARKS MEAN MOVE BEFORE THEY FIRE",174,C_RED);center("MINT MARKS THE SAFE GAP",195,C_MINT);center("CLEAR TO RETURN",221,C_GOLD);
}
static void draw_end(bool win){
  menu_backdrop();if(win){center("THE SEALS ARE BROKEN",48,C_GOLD);center("THE CRYPT OPENS TO DAWN",70,C_MINT);}else{center("THE DARK TAKES YOU",48,C_RED);center("YOUR LANTERN FADES",70,C_WALLHI);}
  box(56,91,208,91,C_VOID);box(59,94,202,85,C_WALL);txt("FLOOR",77,108,C_WALLHI);number(floor_no+1,225,108,C_WHITE);txt("SCORE",77,128,C_WALLHI);number(score,225,128,C_GOLD);txt("BOSSES",77,148,C_WALLHI);number(run_bosses,225,148,C_MINT);txt("DASHES",77,168,C_WALLHI);number(run_dodges,225,168,C_BLUE);
  center("ENTER: MAIN MENU",199,C_WHITE);center("CLEAR: QUIT",217,C_WALLHI);
}
static void paint(void){
  if(state==ST_MENU)draw_menu();else if(state==ST_MODE)draw_mode();else if(state==ST_STATS)draw_stats();else if(state==ST_CONTROLS)draw_controls();else if(state==ST_WIN||state==ST_LOSE)draw_end(state==ST_WIN);else draw_room();
  if(state==ST_PAUSE){box(64,82,192,70,C_VOID);center("PAUSED",98,C_GOLD);center("ENTER RESUME   CLEAR MENU",122,C_WHITE);}
}
static void handle(void){
  kb_Scan();bool l=(kb_Data[7]&kb_Left)!=0,r=(kb_Data[7]&kb_Right)!=0,u=(kb_Data[7]&kb_Up)!=0,d=(kb_Data[7]&kb_Down)!=0;
  bool fire=(kb_Data[1]&kb_2nd)!=0,dash=(kb_Data[2]&kb_Alpha)!=0,enter=(kb_Data[6]&kb_Enter)!=0,clear=(kb_Data[6]&kb_Clear)!=0;
  bool up_edge=u&&!prev_up,down_edge=d&&!prev_down,enter_edge=enter&&!prev_enter,clear_edge=clear&&!prev_clear;
  if(state==ST_MENU){
    if(up_edge)menu_sel=(menu_sel+3)%4;if(down_edge)menu_sel=(menu_sel+1)%4;
    if(enter_edge){if(menu_sel==0){state=ST_MODE;mode_sel=0;}else if(menu_sel==1){records_load();state=ST_STATS;}else if(menu_sel==2)state=ST_CONTROLS;else quit_game=true;}
    if(clear_edge)quit_game=true;
  }else if(state==ST_MODE){
    if(up_edge)mode_sel=(mode_sel+2)%3;if(down_edge)mode_sel=(mode_sel+1)%3;
    if(clear_edge|| (enter_edge&&mode_sel==2))show_menu();
    else if(enter_edge){run_mode=mode_sel;start_run();}
  }else if(state==ST_STATS||state==ST_CONTROLS){if(clear_edge||enter_edge)show_menu();}
  else if(state==ST_WIN||state==ST_LOSE){if(enter_edge)show_menu();if(clear_edge)quit_game=true;}
  else if(state==ST_PAUSE){if(enter_edge)state=paused_state;if(clear_edge){finish_run();show_menu();}}
  else if(clear_edge){paused_state=state;state=ST_PAUSE;}
  else{
    if(dash&&!prev_dash&&dash_cd==0){dash_time=8;dash_cd=54;echo_time=38;echo_x=px;echo_y=py;iframes=9;run_dodges++;sparkle(px,py,C_MINT,10);}
    if(state==ST_BOSS){
      move_player(r-l,d-u);
      if(fire&&fire_cd==0&&!door_open){add_shot(px,py,160,61);fire_cd=shot_level?7:10;}
      boss_update();
      if(door_open&&px>298&&py>102&&py<154)next_room();
    }else explore_update(l,r,u,d,fire);
  }
  prev_dash=dash;prev_enter=enter;prev_clear=clear;prev_up=u;prev_down=d;
  if(state==ST_EXPLORE||state==ST_BOSS){if(iframes)iframes--;if(fire_cd)fire_cd--;if(dash_time)dash_time--;if(dash_cd)dash_cd--;if(echo_time)echo_time--;}
}
int main(void){
  gfx_Begin();gfx_SetDrawBuffer();gfx_SetTextScale(1,1);set_theme(0);gfx_SetTransparentColor(0);
  records_load();
  timer_Enable(1,TIMER_32K,TIMER_NOINT,TIMER_UP);timer_Enable(2,TIMER_32K,TIMER_NOINT,TIMER_UP);
  for(;;){timer_Set(1,0);handle();ticks++;paint();gfx_SwapDraw();while(timer_Get(1)<FPS_TICKS){}if(quit_game)break;}
  gfx_End();return 0;
}
