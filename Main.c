#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspgu.h>
#include <pspgum.h>

#include <stdio.h>
#include <math.h>

PSP_MODULE_INFO("ECOS_DE_NEON", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define W 480
#define H 272
#define MAP_W 1200
#define MAP_H 800
#define SPEED 2.4f

static unsigned int __attribute__((aligned(16))) displayList[262144];

typedef struct {
    float x;
    float y;
    float w;
    float h;
} Object;

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
    int active;
} Particle;

static Object player;
static Object enemy1;
static Object enemy2;
static Object npc;
static Object crystal;
static Object portal;

static Particle particles[32];

static int running = 1;
static int paused = 0;
static int health = 100;
static int energy = 100;
static int coins = 0;
static int mission = 0;
static int dialogue = 0;
static int victory = 0;

static float cameraX = 0;
static float cameraY = 0;

static unsigned int frame = 0;

static int exitCallback(int arg1, int arg2, void *common)
{
    running = 0;
    return 0;
}

static int callbackThread(SceSize args, void *argp)
{
    int cbid;

    cbid = sceKernelCreateCallback(
        "Exit Callback",
        exitCallback,
        NULL
    );

    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();

    return 0;
}

static int setupCallbacks(void)
{
    int thid;

    thid = sceKernelCreateThread(
        "callback_thread",
        callbackThread,
        0x11,
        0xFA0,
        0,
        NULL
    );

    if(thid >= 0)
        sceKernelStartThread(thid, 0, NULL);

    return thid;
}

static void setupGraphics(void)
{
    sceGuInit();

    sceGuStart(
        GU_DIRECT,
        displayList
    );

    sceGuDrawBuffer(
        GU_PSM_8888,
        (void *)0,
        512
    );

    sceGuDispBuffer(
        W,
        H,
        (void *)0x88000,
        512
    );

    sceGuDepthBuffer(
        (void *)0x110000,
        512
    );

    sceGuOffset(
        2048 - W / 2,
        2048 - H / 2
    );

    sceGuViewport(
        2048,
        2048,
        W,
        H
    );

    sceGuScissor(
        0,
        0,
        W,
        H
    );

    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);

    sceGuClearColor(0x080B18FF);

    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();

    sceGuDisplay(GU_TRUE);
}

static void beginFrame(void)
{
    sceGuStart(
        GU_DIRECT,
        displayList
    );

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);

    sceGuClearColor(
        0x080B18FF
    );

    sceGuClear(
        GU_COLOR_BUFFER_BIT
    );
}

static void endFrame(void)
{
    sceGuFinish();

    sceGuSync(
        0,
        0
    );

    sceDisplayWaitVblankStart();

    sceGuSwapBuffers();
}

typedef struct
{
    unsigned int color;
    float x;
    float y;
    float z;
} Vertex;

static void drawRect(
    float x,
    float y,
    float w,
    float h,
    unsigned int color
)
{
    Vertex *v;

    v = (Vertex *)sceGuGetMemory(
        sizeof(Vertex) * 2
    );

    v[0].color = color;
    v[0].x = x;
    v[0].y = y;
    v[0].z = 0;

    v[1].color = color;
    v[1].x = x + w;
    v[1].y = y + h;
    v[1].z = 0;

    sceGuColor(color);

    sceGuDrawArray(
        GU_SPRITES,
        GU_COLOR_8888 |
        GU_VERTEX_32BITF |
        GU_TRANSFORM_2D,
        2,
        0,
        v
    );
}

static float distanceTo(
    float x1,
    float y1,
    float x2,
    float y2
)
{
    float dx = x2 - x1;
    float dy = y2 - y1;

    return sqrtf(
        dx * dx + dy * dy
    );
}

static int collision(
    Object *a,
    Object *b
)
{
    if(
        a->x < b->x + b->w &&
        a->x + a->w > b->x &&
        a->y < b->y + b->h &&
        a->y + a->h > b->y
    )
        return 1;

    return 0;
}

static void createParticles(
    float x,
    float y
)
{
    int i;

    for(i = 0; i < 32; i++)
    {
        particles[i].x = x;
        particles[i].y = y;

        particles[i].vx =
            ((i % 7) - 3) * 0.35f;

        particles[i].vy =
            ((i % 5) - 2) * 0.35f;

        particles[i].active = 1;
    }
}

static void updateParticles(void)
{
    int i;

    for(i = 0; i < 32; i++)
    {
        if(particles[i].active)
        {
            particles[i].x +=
                particles[i].vx;

            particles[i].y +=
                particles[i].vy;

            if(
                particles[i].x < 0 ||
                particles[i].x > MAP_W ||
                particles[i].y < 0 ||
                particles[i].y > MAP_H
            )
            {
                particles[i].active = 0;
            }
        }
    }
}

static void drawParticles(void)
{
    int i;

    for(i = 0; i < 32; i++)
    {
        if(particles[i].active)
        {
            drawRect(
                particles[i].x - cameraX,
                particles[i].y - cameraY,
                3,
                3,
                0x42E8FFFF
            );
        }
    }
}

static void drawGround(void)
{
    int x;
    int y;

    drawRect(
        0,
        0,
        W,
        H,
        0x0A1022FF
    );

    for(y = 0; y < MAP_H; y += 80)
    {
        for(x = 0; x < MAP_W; x += 80)
        {
            float sx = x - cameraX;
            float sy = y - cameraY;

            if(
                sx > -80 &&
                sx < W &&
                sy > -80 &&
                sy < H
            )
            {
                drawRect(
                    sx,
                    sy,
                    78,
                    78,
                    0x101A32FF
                );

                drawRect(
                    sx + 5,
                    sy + 5,
                    2,
                    68,
                    0x172542FF
                );

                drawRect(
                    sx + 5,
                    sy + 5,
                    68,
                    2,
                    0x172542FF
                );
            }
        }
    }
}

static void drawRoads(void)
{
    drawRect(
        -cameraX,
        360 - cameraY,
        MAP_W,
        70,
        0x202B46FF
    );

    drawRect(
        560 - cameraX,
        -cameraY,
        75,
        MAP_H,
        0x202B46FF
    );

    drawRect(
        -cameraX,
        390 - cameraY,
        MAP_W,
        3,
        0x34496FFF
    );

    drawRect(
        595 - cameraX,
        -cameraY,
        3,
        MAP_H,
        0x34496FFF
    );
}

static void drawBuildings(void)
{
    int i;

    float buildings[8][4] =
    {
        {80,80,130,100},
        {300,70,150,130},
        {760,90,180,120},
        {980,260,130,150},
        {120,520,180,130},
        {390,560,120,100},
        {760,520,200,130},
        {970,600,150,100}
    };

    for(i = 0; i < 8; i++)
    {
        float x =
            buildings[i][0] - cameraX;

        float y =
            buildings[i][1] - cameraY;

        float w =
            buildings[i][2];

        float h =
            buildings[i][3];

        if(
            x > W ||
            x + w < 0 ||
            y > H ||
            y + h < 0
        )
            continue;

        drawRect(
            x,
            y,
            w,
            h,
            0x17233FFF
        );

        drawRect(
            x + 8,
            y + 8,
            w - 16,
            8,
            0x304A70FF
        );

        drawRect(
            x + 15,
            y + 30,
            12,
            12,
            0x42E8FFFF
        );

        drawRect(
            x + 45,
            y + 30,
            12,
            12,
            0x42E8FFFF
        );

        drawRect(
            x + 75,
            y + 30,
            12,
            12,
            0x42E8FFFF
        );

        drawRect(
            x + 15,
            y + 60,
            12,
            12,
            0x42E8FFFF
        );

        drawRect(
            x + 45,
            y + 60,
            12,
            12,
            0x42E8FFFF
        );
    }
}

static void drawNeonTower(void)
{
    float x = 540 - cameraX;
    float y = 105 - cameraY;

    drawRect(
        x,
        y,
        110,
        150,
        0x26174AFF
    );

    drawRect(
        x + 35,
        y - 35,
        40,
        35,
        0x38216CFF
    );

    drawRect(
        x + 48,
        y - 80,
        14,
        45,
        0x49EFFFFF
    );

    drawRect(
        x + 10,
        y + 20,
        90,
        8,
        0xB744FFFF
    );

    drawRect(
        x + 20,
        y + 55,
        70,
        8,
        0x49EFFFFF
    );

    drawRect(
        x + 20,
        y + 90,
        70,
        8,
        0xB744FFFF
    );
}

static void drawPlayer(void)
{
    float x =
        player.x - cameraX;

    float y =
        player.y - cameraY;

    drawRect(
        x,
        y,
        player.w,
        player.h,
        0x27DDEBFF
    );

    drawRect(
        x + 4,
        y - 9,
        16,
        9,
        0xC9F8FFFF
    );

    drawRect(
        x + 5,
        y + 8,
        5,
        5,
        0x07121FFF
    );

    drawRect(
        x + 14,
        y + 8,
        5,
        5,
        0x07121FFF
    );

    if(frame % 30 < 15)
    {
        drawRect(
            x - 4,
            y + 28,
            32,
            3,
            0x27DDEBFF
        );
    }
}

static void drawEnemy(Object *e)
{
    float x =
        e->x - cameraX;

    float y =
        e->y - cameraY;

    drawRect(
        x,
        y,
        e->w,
        e->h,
        0xFF3459FF
    );

    drawRect(
        x + 4,
        y + 5,
        6,
        6,
        0xFFFFFFFF
    );

    drawRect(
        x + 14,
        y + 5,
        6,
        6,
        0xFFFFFFFF
    );

    drawRect(
        x + 5,
        y + 18,
        14,
        3,
        0x650A23FF
    );
}

static void drawNPC(void)
{
    float x =
        npc.x - cameraX;

    float y =
        npc.y - cameraY;

    drawRect(
        x,
        y,
        npc.w,
        npc.h,
        0xF3A43BFF
    );

    drawRect(
        x + 4,
        y - 8,
        16,
        8,
        0xFFE0A6FF
    );

    drawRect(
        x + 5,
        y + 8,
        5,
        5,
        0x30200FFF
    );

    drawRect(
        x + 14,
        y + 8,
        5,
        5,
        0x30200FFF
    );
}

static void drawCrystal(void)
{
    if(mission < 2)
    {
        float x =
            crystal.x - cameraX;

        float y =
            crystal.y - cameraY;

        drawRect(
            x + 6,
            y,
            12,
            30,
            0x4AFFFFAA
        );

        drawRect(
            x,
            y + 8,
            24,
            14,
            0x37CFFFFF
        );
    }
}

static void drawPortal(void)
{
    float x =
        portal.x - cameraX;

    float y =
        portal.y - cameraY;

    drawRect(
        x,
        y,
        50,
        70,
        0x29104FFF
    );

    drawRect(
        x + 7,
        y + 7,
        36,
        56,
        0x8A32FFFF
    );

    drawRect(
        x + 14,
        y + 14,
        22,
        42,
        0x40E8FFFF
    );
}

static void drawHUD(void)
{
    int hpWidth =
        health;

    int enWidth =
        energy;

    drawRect(
        10,
        10,
        120,
        42,
        0x07101EFF
    );

    drawRect(
        18,
        18,
        100,
        8,
        0x401525FF
    );

    drawRect(
        18,
        18,
        hpWidth,
        8,
        0xFF3F62FF
    );

    drawRect(
        18,
        34,
        100,
        6,
        0x132E3AFF
    );

    drawRect(
        18,
        34,
        enWidth,
        6,
        0x35E8FFFF
    );

    drawRect(
        350,
        10,
        120,
        42,
        0x07101EFF
    );

    drawRect(
        362,
        20,
        12,
        12,
        0xFFD447FF
    );

    if(coins > 0)
    {
        int cw = coins * 5;

        if(cw > 70)
            cw = 70;

        drawRect(
            380,
            21,
            cw,
            8,
            0xFFD447FF
        );
    }

    drawRect(
        10,
        240,
        200,
        22,
        0x07101EEE
    );

    if(mission == 0)
    {
        drawRect(
            18,
            247,
            120,
            4,
            0x49EFFFFF
        );
    }
    else if(mission == 1)
    {
        drawRect(
            18,
            247,
            155,
            4,
            0xFFD447FF
        );
    }
    else if(mission == 2)
    {
        drawRect(
            18,
            247,
            175,
            4,
            0x49EFFFFF
        );
    }
    else
    {
        drawRect(
            18,
            247,
            190,
            4,
            0x55FF99FF
        );
    }
}

static void drawDialogue(void)
{
    if(!dialogue)
        return;

    drawRect(
        35,
        165,
        410,
        75,
        0x050914F5
    );

    drawRect(
        45,
        175,
        390,
        3,
        0x42E8FFFF
    );

    if(mission == 0)
    {
        drawRect(
            60,
            195,
            270,
            5,
            0xFFFFFFFF
        );

        drawRect(
            60,
            210,
            340,
            5,
            0xFFFFFFFF
        );
    }
    else
    {
        drawRect(
            60,
            195,
            320,
            5,
            0xFFFFFFFF
        );

        drawRect(
            60,
            210,
            260,
            5,
            0xFFFFFFFF
        );
    }
}

static void drawPause(void)
{
    drawRect(
        80,
        55,
        320,
        165,
        0x050914F5
    );

    drawRect(
        100,
        80,
        280,
        8,
        0x42E8FFFF
    );

    drawRect(
        125,
        120,
        230,
        7,
        0xFFFFFFFF
    );

    drawRect(
        125,
        145,
        230,
        7,
        0xFFFFFFFF
    );

    drawRect(
        125,
        170,
        230,
        7,
        0xFFFFFFFF
    );
}

static void drawVictory(void)
{
    drawRect(
        55,
        50,
        370,
        170,
        0x050914F8
    );

    drawRect(
        90,
        80,
        300,
        10,
        0x49EFFFFF
    );

    drawRect(
        120,
        115,
        240,
        8,
        0x55FF99FF
    );

    drawRect(
        120,
        145,
        240,
        8,
        0xFFFFFFFF
    );

    drawRect(
        145,
        180,
        190,
        7,
        0xFFD447FF
    );
}

static void updatePlayer(
    SceCtrlData *pad
)
{
    float speed = SPEED;

    if(pad->Buttons & PSP_CTRL_RTRIGGER)
        speed = 4.0f;

    if(pad->Buttons & PSP_CTRL_UP)
        player.y -= speed;

    if(pad->Buttons & PSP_CTRL_DOWN)
        player.y += speed;

    if(pad->Buttons & PSP_CTRL_LEFT)
        player.x -= speed;

    if(pad->Buttons & PSP_CTRL_RIGHT)
        player.x += speed;

    if(player.x < 20)
        player.x = 20;

    if(player.y < 20)
        player.y = 20;

    if(player.x > MAP_W - player.w - 20)
        player.x = MAP_W - player.w - 20;

    if(player.y > MAP_H - player.h - 20)
        player.y = MAP_H - player.h - 20;

    if(energy < 100)
    {
        if(frame % 20 == 0)
            energy++;
    }
}

static void updateEnemy(Object *enemy)
{
    float dx =
        player.x - enemy->x;

    float dy =
        player.y - enemy->y;

    float d =
        sqrtf(
            dx * dx +
            dy * dy
        );

    if(
        d < 250 &&
        d > 25
    )
    {
        enemy->x +=
            dx / d * 0.65f;

        enemy->y +=
            dy / d * 0.65f;
    }

    if(collision(&player, enemy))
    {
        if(frame % 30 == 0)
        {
            if(health > 0)
                health--;

            createParticles(
                player.x,
                player.y
            );
        }
    }
}

static void updateMission(
    SceCtrlData *pad
)
{
    float npcDistance =
        distanceTo(
            player.x,
            player.y,
            npc.x,
            npc.y
        );

    float crystalDistance =
        distanceTo(
            player.x,
            player.y,
            crystal.x,
            crystal.y
        );

    float portalDistance =
        distanceTo(
            player.x,
            player.y,
            portal.x,
            portal.y
        );

    if(
        mission == 0 &&
        npcDistance < 55
    )
    {
        if(pad->Buttons & PSP_CTRL_CROSS)
        {
            dialogue = 1;
            mission = 1;
        }
    }

    if(
        mission == 1 &&
        crystalDistance < 50
    )
    {
        if(pad->Buttons & PSP_CTRL_CROSS)
        {
            mission = 2;
            coins += 5;

            createParticles(
                crystal.x,
                crystal.y
            );
        }
    }

    if(
        mission == 2 &&
        portalDistance < 70
    )
    {
        if(pad->Buttons & PSP_CTRL_CROSS)
        {
            mission = 3;
            victory = 1;

            createParticles(
                portal.x,
                portal.y
            );
        }
    }
}

static void updateCamera(void)
{
    cameraX =
        player.x - W / 2;

    cameraY =
        player.y - H / 2;

    if(cameraX < 0)
        cameraX = 0;

    if(cameraY < 0)
        cameraY = 0;

    if(cameraX > MAP_W - W)
        cameraX = MAP_W - W;

    if(cameraY > MAP_H - H)
        cameraY = MAP_H - H;
}

static void initGame(void)
{
    player.x = 250;
    player.y = 350;
    player.w = 24;
    player.h = 32;

    enemy1.x = 700;
    enemy1.y = 320;
    enemy1.w = 25;
    enemy1.h = 25;

    enemy2.x = 900;
    enemy2.y = 520;
    enemy2.w = 25;
    enemy2.h = 25;

    npc.x = 180;
    npc.y = 330;
    npc.w = 24;
    npc.h = 32;

    crystal.x = 830;
    crystal.y = 180;
    crystal.w = 24;
    crystal.h = 30;

    portal.x = 1060;
    portal.y = 650;
    portal.w = 50;
    portal.h = 70;

    health = 100;
    energy = 100;
    coins = 0;

    mission = 0;
    dialogue = 0;
    victory = 0;
}

int main(void)
{
    SceCtrlData pad;
    SceCtrlData oldPad;

    oldPad.Buttons = 0;

    setupCallbacks();
    setupGraphics();

    sceCtrlSetSamplingCycle(0);

    sceCtrlSetSamplingMode(
        PSP_CTRL_MODE_ANALOG
    );

    initGame();

    while(running)
    {
        sceCtrlPeekBufferPositive(
            &pad,
            1
        );

        if(
            (pad.Buttons & PSP_CTRL_START) &&
            !(oldPad.Buttons & PSP_CTRL_START)
        )
        {
            paused = !paused;
        }

        if(
            (pad.Buttons & PSP_CTRL_TRIANGLE) &&
            !(oldPad.Buttons & PSP_CTRL_TRIANGLE)
        )
        {
            dialogue = 0;
        }

        if(!paused && !victory)
        {
            updatePlayer(&pad);

            updateEnemy(&enemy1);
            updateEnemy(&enemy2);

            updateMission(&pad);

            updateParticles();

            updateCamera();
        }

        beginFrame();

        drawGround();
        drawRoads();
        drawBuildings();
        drawNeonTower();
        drawParticles();
        drawCrystal();
        drawPortal();
        drawNPC();
        drawEnemy(&enemy1);
        drawEnemy(&enemy2);
        drawPlayer();
        drawHUD();

        if(dialogue)
            drawDialogue();

        if(paused)
            drawPause();

        if(victory)
            drawVictory();

        endFrame();

        oldPad = pad;

        frame++;
    }

    sceGuTerm();

    sceKernelExitGame();

    return 0;
}
