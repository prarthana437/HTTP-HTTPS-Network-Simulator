#include "raylib.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* =========================================================
   HTTP / HTTPS NETWORK SIMULATOR  (classroom version)
   ---------------------------------------------------------
   - 1600 x 900 canvas scaled to fill the window (F11 = full screen)
   - Packet travels on a RECTANGULAR route:
        request  : client -> up -> right (top lane)  -> down -> server
        response : server -> down -> left (bottom lane) -> up -> client
   - Packet is drawn as a bar of layers:  [ETH][IP][TCP][HTTP][FCS]
   - Controls: SEND, PAUSE/RESUME, PREVIOUS, NEXT, RESET,
               MANUAL/AUTO mode, SLOW/NORMAL/FAST speed
   - Everything is SIMULATED (no real packets, no real TLS)
   ========================================================= */


/* =========================================================
   UI / ROUTE CONSTANTS
   ========================================================= */

#define UI_WIDTH          1600.0f
#define UI_HEIGHT         900.0f

#define CLIENT_SRC_PORT   52314

#define BOX_W             250.0f
#define BOX_H             150.0f
#define BOX_Y             414.0f
#define CLIENT_BOX_X      50.0f
#define SERVER_BOX_X      1300.0f

#define CLIENT_CX         175.0f      /* centre x of client box */
#define SERVER_CX         1425.0f     /* centre x of server box */

#define LANE_T            374.0f      /* top lane (request)     */
#define LANE_B            594.0f      /* bottom lane (response) */

#define ENC_X             420.0f      /* packet is built / opened here (client side) */
#define DEC_X             1180.0f     /* packet is built / opened here (server side) */

#define HOLD_TIME         3.0f        /* extra seconds each step stays on screen in AUTO mode */


/* =========================================================
   STAGES  (order matters: stage + 1 = next stage)
   ========================================================= */

typedef enum
{
    IDLE,

    CLIENT_CREATES,
    HTTP_REQUEST,
    TCP_SEGMENT,
    IP_PACKET,
    FRAME,
    NETWORK,
    SERVER_RECEIVES,
    SERVER_PROCESSING,

    HTTP_RESPONSE,
    RESPONSE_TCP,
    RESPONSE_IP,
    RESPONSE_FRAME,
    RESPONSE_NETWORK,

    COMPLETE

} Stage;

#define STEP_COUNT   14      /* CLIENT_CREATES ... COMPLETE */


typedef enum
{
    HTTP,
    HTTPS

} Protocol;


/* =========================================================
   DATA STRUCTURES
   ========================================================= */

typedef struct
{
    char method[10];
    char path[100];
    char host[100];
    char body[250];

    Protocol protocol;

} HttpRequest;


typedef struct
{
    int  statusCode;
    char statusText[50];
    char body[300];
    char explain[160];

} HttpResponse;


typedef struct
{
    char source[50];
    char destination[50];

    int srcPort;
    int port;           /* destination port of the request */

} TCPSegment;


typedef struct
{
    char sourceIP[50];
    char destinationIP[50];

} IPPacket;


typedef struct
{
    char sourceMAC[50];
    char destinationMAC[50];

} Frame;


typedef struct
{
    char speaker[48];
    char say[300];
    char tech[300];
    char job[300];
    char adds[300];
    char next[300];

} Explain;


/* =========================================================
   GLOBALS
   ========================================================= */

Font uiFont;

static float   uiScale  = 1.0f;
static Vector2 uiOffset = { 0.0f, 0.0f };


/* =========================================================
   COLOUR PALETTE
   ========================================================= */

Color bgColor     = { 238, 242, 247, 255 };
Color panelColor  = { 255, 255, 255, 255 };
Color panelLight  = { 244, 247, 250, 255 };
Color blue        = {  45, 115, 210, 255 };
Color cyan        = {  20, 160, 180, 255 };
Color green       = {  40, 165,  95, 255 };
Color orange      = { 225, 135,  35, 255 };
Color red         = { 215,  65,  65, 255 };
Color purple      = { 125,  95, 205, 255 };
Color white       = { 255, 255, 255, 255 };
Color gray        = {  75,  90, 105, 255 };
Color darkGray    = { 170, 180, 192, 255 };
Color textColor   = {  25,  35,  50, 255 };
Color lineColor   = { 205, 213, 222, 255 };
Color placeholder = { 140, 150, 162, 255 };
Color idleGray    = { 130, 142, 156, 255 };
Color deviceDark  = {  60,  72,  88, 255 };
Color dataGray    = { 105, 125, 155, 255 };

/* colour of each layer: HTTP, TCP, IP, FRAME */
Color layerColors[4] =
{
    {  45, 115, 210, 255 },
    {  40, 165,  95, 255 },
    {  20, 160, 180, 255 },
    { 225, 135,  35, 255 }
};


/* =========================================================
   LAYOUT RECTANGLES (shared by drawing AND mouse clicks)
   ========================================================= */

/* request builder */
static const Rectangle btnGet      = {   40, 116, 100, 38 };
static const Rectangle btnPost     = {  150, 116, 100, 38 };
static const Rectangle inHost      = {  290, 116, 370, 38 };
static const Rectangle inPath      = {  680, 116, 290, 38 };
static const Rectangle inBody      = {  990, 116, 560, 38 };

static const Rectangle btnProtocol = {   40, 198, 150, 38 };
static const Rectangle btnSuccess  = {  490, 198, 110, 38 };
static const Rectangle btnFailure  = {  608, 198, 110, 38 };
static const Rectangle chip400     = {  740, 198,  66, 38 };
static const Rectangle chip404     = {  812, 198,  66, 38 };
static const Rectangle chip500     = {  884, 198,  66, 38 };

/* control bar */
static const Rectangle btnSend     = {   40, 266, 170, 36 };
static const Rectangle btnPause    = {  220, 266, 130, 36 };
static const Rectangle btnPrev     = {  360, 266, 130, 36 };
static const Rectangle btnNext     = {  500, 266, 130, 36 };
static const Rectangle btnReset    = {  640, 266, 100, 36 };
static const Rectangle btnManual   = {  830, 266, 100, 36 };
static const Rectangle btnAuto     = {  938, 266,  80, 36 };
static const Rectangle btnSlow     = { 1115, 266,  80, 36 };
static const Rectangle btnNormal   = { 1203, 266,  95, 36 };
static const Rectangle btnFast     = { 1306, 266,  80, 36 };

/* route polylines (axis aligned: only horizontal / vertical segments) */
static const Vector2 reqStart[3]  = { { CLIENT_CX, BOX_Y }, { CLIENT_CX, LANE_T }, { ENC_X, LANE_T } };
static const Vector2 respStart[3] = { { SERVER_CX, BOX_Y + BOX_H }, { SERVER_CX, LANE_B }, { DEC_X, LANE_B } };


/* =========================================================
   SCALING  (makes the UI fill the whole window)
   ========================================================= */

void UpdateUIScale(void)
{
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();

    float sx = sw / UI_WIDTH;
    float sy = sh / UI_HEIGHT;

    uiScale = (sx < sy) ? sx : sy;

    if (uiScale < 0.05f)
        uiScale = 0.05f;

    uiOffset.x = (sw - UI_WIDTH  * uiScale) * 0.5f;
    uiOffset.y = (sh - UI_HEIGHT * uiScale) * 0.5f;
}


Vector2 GetUIMousePosition(void)
{
    Vector2 m = GetMousePosition();

    return (Vector2)
    {
        (m.x - uiOffset.x) / uiScale,
        (m.y - uiOffset.y) / uiScale
    };
}


bool Clicked(Rectangle r)
{
    return IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
           CheckCollisionPointRec(GetUIMousePosition(), r);
}


/* =========================================================
   SMALL HELPERS
   ========================================================= */

unsigned char AddClamp(unsigned char c, int v)
{
    int r = c + v;
    return (unsigned char)(r > 255 ? 255 : (r < 0 ? 0 : r));
}

float AbsF(float v)   { return v < 0 ? -v : v; }

float ClampF(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int ClampI(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float Ease(float t)
{
    t = ClampF(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* opaque lighter version of a colour (mix with white) */
Color Pale(Color c, float k)
{
    return (Color)
    {
        (unsigned char)(c.r + (255 - c.r) * k),
        (unsigned char)(c.g + (255 - c.g) * k),
        (unsigned char)(c.b + (255 - c.b) * k),
        255
    };
}

/* point at fraction u (0..1) along an axis-aligned polyline */
Vector2 PathPoint(const Vector2 *p, int n, float u)
{
    float total = 0.0f;

    for (int i = 0; i < n - 1; i++)
        total += AbsF(p[i + 1].x - p[i].x) + AbsF(p[i + 1].y - p[i].y);

    float d = ClampF(u, 0.0f, 1.0f) * total;

    for (int i = 0; i < n - 1; i++)
    {
        float seg = AbsF(p[i + 1].x - p[i].x) + AbsF(p[i + 1].y - p[i].y);

        if (d <= seg || i == n - 2)
        {
            float k = (seg > 0.0f) ? d / seg : 0.0f;
            k = ClampF(k, 0.0f, 1.0f);

            return (Vector2)
            {
                p[i].x + (p[i + 1].x - p[i].x) * k,
                p[i].y + (p[i + 1].y - p[i].y) * k
            };
        }

        d -= seg;
    }

    return p[n - 1];
}


/* =========================================================
   SIMULATION SETTINGS
   ========================================================= */

float SpeedFactor(int idx)
{
    if (idx == 0) return 0.6f;     /* SLOW   */
    if (idx == 1) return 1.0f;     /* NORMAL */
    return 2.0f;                   /* FAST   */
}


/* animation time (seconds at NORMAL speed) */
float AnimDur(Stage s)
{
    switch (s)
    {
        case CLIENT_CREATES:     return 1.5f;
        case NETWORK:
        case RESPONSE_NETWORK:   return 3.5f;
        case SERVER_RECEIVES:    return 2.0f;
        case SERVER_PROCESSING:  return 2.0f;
        case HTTP_RESPONSE:      return 1.5f;
        case COMPLETE:           return 2.0f;
        default:                 return 1.0f;
    }
}


/* =========================================================
   HTTP RESPONSE BUILDER
   ========================================================= */

HttpResponse BuildResponse(const HttpRequest *req, int code)
{
    HttpResponse r;
    memset(&r, 0, sizeof(r));

    bool isPost = (strcmp(req->method, "POST") == 0);
    const char *path = req->path[0] ? req->path : "/";

    r.statusCode = code;

    switch (code)
    {
        case 200:
            strcpy(r.statusText, "OK");
            if (isPost)
                snprintf(r.body, sizeof(r.body), "Data sent to %.80s was accepted.", path);
            else
                snprintf(r.body, sizeof(r.body), "Resource %.80s found and returned.", path);
            strcpy(r.explain, "The request was successful.");
            break;

        case 400:
            strcpy(r.statusText, "Bad Request");
            snprintf(r.body, sizeof(r.body), "The server could not understand the request for %.80s.", path);
            strcpy(r.explain, "The server could not understand the request.");
            break;

        case 404:
            strcpy(r.statusText, "Not Found");
            snprintf(r.body, sizeof(r.body), "The server could not find %.80s.", path);
            strcpy(r.explain, "The server could not find the requested resource.");
            break;

        default:
            r.statusCode = 500;
            strcpy(r.statusText, "Internal Server Error");
            snprintf(r.body, sizeof(r.body), "The server had a problem while handling %.80s.", path);
            strcpy(r.explain, "The server hit an unexpected problem.");
            break;
    }

    return r;
}


HttpRequest CreateDefaultRequest(void)
{
    HttpRequest request;
    memset(&request, 0, sizeof(request));

    strcpy(request.method, "GET");
    strcpy(request.path,   "/users");
    strcpy(request.host,   "www.google.com");
    request.body[0] = '\0';
    request.protocol = HTTPS;

    return request;
}


/* =========================================================
   STAGE TEXT HELPERS
   ========================================================= */

const char *GetStageName(Stage stage)
{
    switch (stage)
    {
        case CLIENT_CREATES:    return "CLIENT CREATES REQUEST";
        case HTTP_REQUEST:      return "HTTP REQUEST (APPLICATION)";
        case TCP_SEGMENT:       return "TCP SEGMENT (TRANSPORT)";
        case IP_PACKET:         return "IP PACKET (NETWORK)";
        case FRAME:             return "ETHERNET FRAME (DATA LINK)";
        case NETWORK:           return "NETWORK TRANSMISSION";
        case SERVER_RECEIVES:   return "SERVER RECEIVES REQUEST";
        case SERVER_PROCESSING: return "SERVER PROCESSING";
        case HTTP_RESPONSE:     return "HTTP RESPONSE (APPLICATION)";
        case RESPONSE_TCP:      return "RESPONSE TCP SEGMENT";
        case RESPONSE_IP:       return "RESPONSE IP PACKET";
        case RESPONSE_FRAME:    return "RESPONSE ETHERNET FRAME";
        case RESPONSE_NETWORK:  return "RESPONSE NETWORK TRANSMISSION";
        case COMPLETE:          return "TRANSACTION COMPLETE";
        default:                return "READY";
    }
}


const char *PacketCaption(Stage stage)
{
    switch (stage)
    {
        case CLIENT_CREATES: return "DATA TO SEND";
        case HTTP_REQUEST:   return "HTTP REQUEST";
        case HTTP_RESPONSE:  return "HTTP RESPONSE";
        case TCP_SEGMENT:
        case RESPONSE_TCP:   return "TCP SEGMENT";
        case IP_PACKET:
        case RESPONSE_IP:    return "IP PACKET";
        case FRAME:
        case RESPONSE_FRAME: return "ETHERNET FRAME";
        default:             return "";
    }
}


/* which layer is highlighted: 0 App, 1 Transport, 2 Network, 3 Data link */
int ActiveLayer(Stage s)
{
    switch (s)
    {
        case CLIENT_CREATES:
        case HTTP_REQUEST:
        case SERVER_PROCESSING:
        case HTTP_RESPONSE:      return 0;

        case TCP_SEGMENT:
        case RESPONSE_TCP:       return 1;

        case IP_PACKET:
        case RESPONSE_IP:        return 2;

        case FRAME:
        case NETWORK:
        case RESPONSE_FRAME:
        case RESPONSE_NETWORK:   return 3;

        default:                 return -1;
    }
}


/* =========================================================
   EXPLANATIONS (speech bubble content)
   ========================================================= */

#define SET(field, ...)  snprintf(e->field, sizeof(e->field), __VA_ARGS__)

void BuildExplain(Stage stage,
                  const HttpRequest *req,
                  const HttpResponse *res,
                  const TCPSegment *tcp,
                  const IPPacket *ip,
                  const Frame *fr,
                  Explain *e)
{
    bool secure       = (req->protocol == HTTPS);
    const char *proto = secure ? "HTTPS" : "HTTP";
    const char *host  = req->host[0] ? req->host : "(no host)";
    const char *path  = req->path[0] ? req->path : "/";

    memset(e, 0, sizeof(*e));

    switch (stage)
    {
        case CLIENT_CREATES:
            SET(speaker, "CLIENT");
            SET(say,  "I want some information from the server, so I am preparing a request.");
            SET(tech, "The user asks for  %s %.40s  on  %.40s.", req->method, path, host);
            SET(job,  "The client decides WHAT to ask for (the path) and WHO to ask (the host).");
            SET(adds, "Nothing is added yet. This is only the data the client wants to send.");
            SET(next, "The Application layer will turn it into an HTTP request.");
            break;

        case HTTP_REQUEST:
            SET(speaker, "CLIENT  -  APPLICATION LAYER");
            SET(say,  "The client creates an HTTP request containing the method, the host and the requested path.");
            SET(tech, "Request line:  %s %.40s HTTP/1.1      Host: %.40s", req->method, path, host);
            SET(job,  "The Application layer writes the message in HTTP, the language web servers understand.");
            SET(adds, "%s", secure ? "Method, host, path (and body for POST). TLS protection is SIMULATED here."
                                   : "Method, host, path (and body for POST). No TLS: data is plain text.");
            SET(next, "The Transport layer will add TCP port information.");
            break;

        case TCP_SEGMENT:
            SET(speaker, "CLIENT  -  TRANSPORT LAYER");
            SET(say,  "TCP adds port information and helps deliver the data reliably.");
            SET(tech, "TCP segment:  source port %d  ->  destination port %d (%s)", tcp->srcPort, tcp->port, proto);
            SET(job,  "The Transport layer makes sure data reaches the right program and arrives in order.");
            SET(adds, "A TCP header: source port, destination port and sequence numbers.");
            SET(next, "The Network layer will add IP addresses.");
            break;

        case IP_PACKET:
            SET(speaker, "CLIENT  -  NETWORK LAYER");
            SET(say,  "IP adds source and destination IP addresses so the packet can travel to the correct device.");
            SET(tech, "IP packet:  %s  ->  %s  (IPv4)", ip->sourceIP, ip->destinationIP);
            SET(job,  "The Network layer handles addressing and finding a route to the destination.");
            SET(adds, "An IP header: source IP, destination IP and a time-to-live (TTL).");
            SET(next, "The Data Link layer will build a frame.");
            break;

        case FRAME:
            SET(speaker, "CLIENT  -  DATA LINK LAYER");
            SET(say,  "The data is organized into a frame for transmission across the local network.");
            SET(tech, "Ethernet frame:  source MAC %s  ->  destination MAC %s", fr->sourceMAC, fr->destinationMAC);
            SET(job,  "The Data Link layer delivers data between neighbouring devices using MAC addresses.");
            SET(adds, "A frame header (MAC addresses) and a trailer (FCS) used for error checking.");
            SET(next, "The frame will travel through the network toward the server.");
            break;

        case NETWORK:
            SET(speaker, "NETWORK");
            SET(say,  "The packet is travelling through network devices toward the server.");
            SET(tech, "Switches read MAC addresses and routers read IP addresses to pass the data along.");
            SET(job,  "Network devices forward the data hop by hop until it reaches the server.");
            SET(adds, "Nothing new is added. (Simulated: real routers also rewrite MAC addresses at each hop.)");
            SET(next, "The server will receive the packet and unwrap it.");
            break;

        case SERVER_RECEIVES:
            SET(speaker, "SERVER");
            SET(say,  "The server has received the request and is unwrapping it layer by layer.");
            SET(tech, "%s", secure ? "Frame, IP and TCP headers are removed; TLS decrypts the data (simulated)."
                                   : "Frame, IP and TCP headers are removed one by one (no TLS to decrypt).");
            SET(job,  "Each layer removes its own header and passes the rest upward (decapsulation).");
            SET(adds, "Nothing is added. The server now sees the request:  %s %.40s", req->method, path);
            SET(next, "The server will process the request.");
            break;

        case SERVER_PROCESSING:
            SET(speaker, "SERVER  -  APPLICATION LAYER");
            SET(say,  "The server is checking what the client asked for and preparing a response.");
            SET(tech, "The server read  %s %.40s  on  %.40s  and decided:  %d %s", req->method, path, host,
                res->statusCode, res->statusText);
            SET(job,  "The server program looks for the page or data, or saves the data sent with a POST.");
            SET(adds, "Result: %d - %.70s", res->statusCode, res->explain);
            SET(next, "The server will create the HTTP response.");
            break;

        case HTTP_RESPONSE:
            SET(speaker, "SERVER  -  APPLICATION LAYER");
            SET(say,  "My answer is %d %s. %s", res->statusCode, res->statusText, res->explain);
            SET(tech, "Status line:  HTTP/1.1 %d %s      Body: %.45s", res->statusCode, res->statusText, res->body);
            SET(job,  "The Application layer writes the HTTP response with a status code and a message.");
            SET(adds, "%s", secure ? "TLS protects (encrypts) the response too - SIMULATED."
                                   : "No TLS: the response is plain text.");
            SET(next, "TCP, IP and Frame headers will be added for the trip back.");
            break;

        case RESPONSE_TCP:
            SET(speaker, "SERVER  -  TRANSPORT LAYER");
            SET(say,  "TCP adds port information again, this time for the reply.");
            SET(tech, "TCP segment:  source port %d  ->  destination port %d", tcp->port, tcp->srcPort);
            SET(job,  "The Transport layer makes sure data reaches the right program and arrives in order.");
            SET(adds, "A TCP header with swapped ports and an acknowledgement (ACK).");
            SET(next, "The Network layer will add IP addresses.");
            break;

        case RESPONSE_IP:
            SET(speaker, "SERVER  -  NETWORK LAYER");
            SET(say,  "IP adds addresses again. Now the server is the sender and the client is the receiver.");
            SET(tech, "IP packet:  %s  ->  %s  (IPv4)", ip->destinationIP, ip->sourceIP);
            SET(job,  "The Network layer handles addressing and finding a route to the destination.");
            SET(adds, "An IP header with the source and destination addresses swapped.");
            SET(next, "The Data Link layer will build a frame.");
            break;

        case RESPONSE_FRAME:
            SET(speaker, "SERVER  -  DATA LINK LAYER");
            SET(say,  "The response is organized into a frame for the trip back across the network.");
            SET(tech, "Ethernet frame:  source MAC %s  ->  destination MAC %s", fr->destinationMAC, fr->sourceMAC);
            SET(job,  "The Data Link layer delivers data between neighbouring devices using MAC addresses.");
            SET(adds, "A frame header (MAC addresses) and a trailer (FCS) used for error checking.");
            SET(next, "The frame will travel back through the network to the client.");
            break;

        case RESPONSE_NETWORK:
            SET(speaker, "NETWORK");
            SET(say,  "The response is travelling back through the network toward the client.");
            SET(tech, "The same kinds of devices pass the data along, now in the opposite direction.");
            SET(job,  "Network devices forward the data hop by hop until it reaches the client.");
            SET(adds, "Nothing new is added during the trip.");
            SET(next, "The client will unwrap the response.");
            break;

        case COMPLETE:
            SET(speaker, "CLIENT");
            SET(say,  "The client has received the server's response: %d %s. %s",
                res->statusCode, res->statusText, res->explain);
            SET(tech, "%s", secure ? "Frame, IP and TCP headers are removed; TLS decrypts the data (simulated)."
                                   : "Frame, IP and TCP headers are removed one by one, then the response is read.");
            SET(job,  "The client (browser) uses the status code to decide what to show the user.");
            SET(adds, "Final result: %s  (status %d).", res->statusCode < 400 ? "SUCCESS" : "FAILED", res->statusCode);
            SET(next, "Press SEND AGAIN to repeat, or change the settings first.");
            break;

        default:
            SET(speaker, "READY");
            SET(say,  "Fill in the request above, then press SEND REQUEST (or NEXT STEP) to watch it travel.");
            SET(tech, "Nothing is being sent yet. All values shown in this program are simulated.");
            SET(job,  "MANUAL mode waits for NEXT STEP. AUTO mode plays the whole journey by itself.");
            SET(adds, "Tip: SLOW speed gives you more time to read every explanation.");
            SET(next, "Press SEND REQUEST to begin.");
            break;
    }
}


/* =========================================================
   LAYER BOX CONTENT (packet information)
   ========================================================= */

int BuildLayerLines(int layer, bool resp,
                    const HttpRequest *req,
                    const HttpResponse *res,
                    const TCPSegment *tcp,
                    const IPPacket *ip,
                    const Frame *fr,
                    char L[5][128])
{
    bool secure = (req->protocol == HTTPS);

    switch (layer)
    {
        case 0:
            if (!resp)
            {
                snprintf(L[0], 128, "Method: %s", req->method);
                snprintf(L[1], 128, "Host: %.40s", req->host[0] ? req->host : "(no host)");
                snprintf(L[2], 128, "Path: %.40s", req->path[0] ? req->path : "/");
                snprintf(L[3], 128, "Body: %.38s", req->body[0] ? req->body : "(empty)");
                snprintf(L[4], 128, "Request line: %s %.26s HTTP/1.1", req->method,
                         req->path[0] ? req->path : "/");
            }
            else
            {
                snprintf(L[0], 128, "Status: %d %s", res->statusCode, res->statusText);
                snprintf(L[1], 128, "Body: %.38s", res->body);
                snprintf(L[2], 128, "Result: %s", res->statusCode < 400 ? "SUCCESS" : "FAILED");
                snprintf(L[3], 128, "Response line: HTTP/1.1 %d %s", res->statusCode, res->statusText);
                snprintf(L[4], 128, "Security: %s", secure ? "HTTPS (TLS simulated)" : "HTTP (not encrypted)");
            }
            return 5;

        case 1:
            snprintf(L[0], 128, "Protocol: TCP");
            snprintf(L[1], 128, "Source port: %d", resp ? tcp->port : tcp->srcPort);
            snprintf(L[2], 128, "Destination port: %d%s", resp ? tcp->srcPort : tcp->port,
                     resp ? "" : (secure ? " (HTTPS)" : " (HTTP)"));
            snprintf(L[3], 128, "Segment: ports + sequence numbers");
            snprintf(L[4], 128, "Purpose: reliable delivery");
            return 5;

        case 2:
            snprintf(L[0], 128, "Version: IPv4");
            snprintf(L[1], 128, "Source IP: %s", resp ? ip->destinationIP : ip->sourceIP);
            snprintf(L[2], 128, "Destination IP: %s", resp ? ip->sourceIP : ip->destinationIP);
            snprintf(L[3], 128, "Protocol field: TCP (6)");
            snprintf(L[4], 128, "TTL: 64");
            return 5;

        default:
            snprintf(L[0], 128, "Type: Ethernet frame");
            snprintf(L[1], 128, "Source MAC: %s", resp ? fr->destinationMAC : fr->sourceMAC);
            snprintf(L[2], 128, "Dest. MAC: %s", resp ? fr->sourceMAC : fr->destinationMAC);
            snprintf(L[3], 128, "Trailer: FCS (error check)");
            snprintf(L[4], 128, "Values are simulated");
            return 5;
    }
}


/* hint lines shown for a layer that has not been added yet */
int BuildLayerHints(int layer, char L[5][128])
{
    switch (layer)
    {
        case 1:
            snprintf(L[0], 128, "Not added yet.");
            snprintf(L[1], 128, "The Transport layer will add:");
            snprintf(L[2], 128, "- source and destination ports");
            snprintf(L[3], 128, "- TCP sequence numbers");
            return 4;

        case 2:
            snprintf(L[0], 128, "Not added yet.");
            snprintf(L[1], 128, "The Network layer will add:");
            snprintf(L[2], 128, "- source IP address");
            snprintf(L[3], 128, "- destination IP address");
            return 4;

        default:
            snprintf(L[0], 128, "Not added yet.");
            snprintf(L[1], 128, "The Data Link layer will add:");
            snprintf(L[2], 128, "- source and destination MAC");
            snprintf(L[3], 128, "- frame trailer (error check)");
            return 4;
    }
}


/* =========================================================
   TEXT DRAWING
   ========================================================= */

void DrawUIText(const char *text, float x, float y, float fontSize, Color color)
{
    DrawTextEx(uiFont, text, (Vector2){ x, y }, fontSize, 1.0f, color);
}


/* shrinks the text slightly if it would not fit in maxW */
void DrawFitText(const char *text, float x, float y, float maxW, float size, Color color)
{
    float s = size;

    while (s > 11.0f && MeasureTextEx(uiFont, text, s, 1.0f).x > maxW)
        s -= 0.5f;

    DrawUIText(text, x, y, s, color);
}


void DrawCenteredText(const char *text, Rectangle r, float fontSize, Color color)
{
    Vector2 m = MeasureTextEx(uiFont, text, fontSize, 1.0f);

    DrawUIText(text,
               r.x + (r.width  - m.x) / 2.0f,
               r.y + (r.height - fontSize) / 2.0f - 1.0f,
               fontSize,
               color);
}


/* word-wrapped text, returns number of lines drawn */
int DrawWrapped(const char *text, float x, float y, float maxW,
                float size, float lineH, Color color, int maxLines)
{
    char line[300];
    char word[128];
    char test[300];
    int  lines = 0;
    const char *p = text;

    line[0] = '\0';

    while (*p && lines < maxLines)
    {
        int wl = 0;

        while (*p == ' ')
            p++;

        while (*p && *p != ' ' && wl < 127)
            word[wl++] = *p++;

        word[wl] = '\0';

        if (wl == 0)
            break;

        if (line[0])
            snprintf(test, sizeof(test), "%s %s", line, word);
        else
            snprintf(test, sizeof(test), "%s", word);

        if (line[0] && MeasureTextEx(uiFont, test, size, 1.0f).x > maxW)
        {
            DrawUIText(line, x, y + lines * lineH, size, color);
            lines++;
            snprintf(line, sizeof(line), "%s", word);
        }
        else
        {
            snprintf(line, sizeof(line), "%s", test);
        }
    }

    if (line[0] && lines < maxLines)
    {
        DrawUIText(line, x, y + lines * lineH, size, color);
        lines++;
    }

    return lines;
}


/* =========================================================
   UI WIDGETS
   ========================================================= */

void DrawPanel(float x, float y, float w, float h, const char *title)
{
    Rectangle r = { x, y, w, h };

    DrawRectangleRec(r, panelColor);
    DrawRectangleLinesEx(r, 1, darkGray);

    DrawUIText(title, x + 18, y + 6, 18, textColor);
}


void DrawButton(const char *text, Rectangle r, Color color, bool enabled)
{
    bool hover = enabled && CheckCollisionPointRec(GetUIMousePosition(), r);

    Color fill = color;

    if (!enabled)
        fill = Pale(color, 0.55f);
    else if (hover)
        fill = (Color){ AddClamp(color.r, 18), AddClamp(color.g, 18), AddClamp(color.b, 18), 255 };

    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, 2, enabled ? color : Pale(color, 0.4f));

    DrawCenteredText(text, r, 18, white);
}


void DrawToggle(const char *text, Rectangle r, Color color, bool selected, bool enabled)
{
    bool hover = enabled && CheckCollisionPointRec(GetUIMousePosition(), r);

    Color fill = selected ? color : (hover ? lineColor : panelLight);

    if (selected && !enabled)
        fill = Pale(color, 0.45f);

    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, 2, selected ? (enabled ? color : Pale(color, 0.45f)) : darkGray);

    DrawCenteredText(text, r, 18, selected ? white : gray);
}


void DrawInputBox(const char *label, const char *value, const char *hint, const char *help,
                  Rectangle r, bool active, bool enabled)
{
    DrawUIText(label, r.x, r.y - 20, 15, gray);

    DrawRectangleRec(r, enabled ? panelLight : (Color){ 230, 234, 240, 255 });
    DrawRectangleLinesEx(r, 2, active ? cyan : darkGray);

    DrawUIText(help, r.x, r.y + r.height + 4, 14, gray);

    float textY = r.y + (r.height - 20) / 2.0f - 1.0f;

    if (value[0] == '\0' && !active)
    {
        DrawUIText(hint, r.x + 14, textY, 20, placeholder);
        return;
    }

    char shown[300];

    bool caret = active && ((int)(GetTime() * 2.0) % 2 == 0);

    snprintf(shown, sizeof(shown), "%s%s", value, caret ? "|" : "");

    /* keep the END of the text visible if it is too long */
    const char *p = shown;
    float maxW = r.width - 28;

    while (*p && MeasureTextEx(uiFont, p, 20, 1.0f).x > maxW)
        p++;

    DrawUIText(p, r.x + 14, textY, 20, textColor);
}


/* straight line segment with square ends (horizontal or vertical only) */
void DrawSeg(float x1, float y1, float x2, float y2, float th, Color c)
{
    float x = ((x1 < x2) ? x1 : x2) - th / 2.0f;
    float y = ((y1 < y2) ? y1 : y2) - th / 2.0f;
    float w = AbsF(x2 - x1) + th;
    float h = AbsF(y2 - y1) + th;

    DrawRectangle((int)x, (int)y, (int)w, (int)h, c);
}


/* dir: 0 right, 1 left, 2 down, 3 up */
void DrawChevron(float x, float y, int dir, Color c)
{
    float s = 9.0f;

    switch (dir)
    {
        case 0:
            DrawLineEx((Vector2){ x - s, y - s }, (Vector2){ x, y }, 3, c);
            DrawLineEx((Vector2){ x - s, y + s }, (Vector2){ x, y }, 3, c);
            break;
        case 1:
            DrawLineEx((Vector2){ x + s, y - s }, (Vector2){ x, y }, 3, c);
            DrawLineEx((Vector2){ x + s, y + s }, (Vector2){ x, y }, 3, c);
            break;
        case 2:
            DrawLineEx((Vector2){ x - s, y - s }, (Vector2){ x, y }, 3, c);
            DrawLineEx((Vector2){ x + s, y - s }, (Vector2){ x, y }, 3, c);
            break;
        default:
            DrawLineEx((Vector2){ x - s, y + s }, (Vector2){ x, y }, 3, c);
            DrawLineEx((Vector2){ x + s, y + s }, (Vector2){ x, y }, 3, c);
            break;
    }
}


/* the rectangular route: request on top, response on the bottom */
void DrawRoute(bool reqActive, bool respActive)
{
    float th = 5.0f;

    Color rc = reqActive  ? blue   : Pale(blue, 0.65f);
    Color pc = respActive ? purple : Pale(purple, 0.65f);

    Color rch = reqActive  ? (Color){  25,  75, 160, 255 } : Pale((Color){  25,  75, 160, 255 }, 0.5f);
    Color pch = respActive ? (Color){  85,  55, 165, 255 } : Pale((Color){  85,  55, 165, 255 }, 0.5f);

    /* request: client top -> up -> right -> down -> server top */
    DrawSeg(CLIENT_CX, BOX_Y,  CLIENT_CX, LANE_T, th, rc);
    DrawSeg(CLIENT_CX, LANE_T, SERVER_CX, LANE_T, th, rc);
    DrawSeg(SERVER_CX, LANE_T, SERVER_CX, BOX_Y,  th, rc);

    /* response: server bottom -> down -> left -> up -> client bottom */
    DrawSeg(SERVER_CX, BOX_Y + BOX_H, SERVER_CX, LANE_B, th, pc);
    DrawSeg(SERVER_CX, LANE_B, CLIENT_CX, LANE_B, th, pc);
    DrawSeg(CLIENT_CX, LANE_B, CLIENT_CX, BOX_Y + BOX_H, th, pc);

    /* direction arrows */
    DrawChevron(CLIENT_CX, 396.0f, 3, rch);
    DrawChevron(SERVER_CX, 396.0f, 2, rch);

    for (float x = 330.0f; x < 1400.0f; x += 190.0f)
    {
        DrawChevron(x, LANE_T, 0, rch);
        DrawChevron(x, LANE_B, 1, pch);
    }

    DrawChevron(SERVER_CX, 580.0f, 2, pch);
    DrawChevron(CLIENT_CX, 580.0f, 3, pch);
}


/* ---------- icons ---------- */

void DrawComputerIcon(float x, float y, bool active)
{
    DrawRectangle((int)(x + 4),  (int)y,        48, 34, deviceDark);
    DrawRectangle((int)(x + 8),  (int)(y + 4),  40, 26, active ? (Color){ 170, 225, 240, 255 } : (Color){ 205, 215, 228, 255 });
    DrawRectangle((int)(x + 24), (int)(y + 34), 8,  6,  deviceDark);
    DrawRectangle((int)(x + 14), (int)(y + 40), 28, 4,  deviceDark);
    DrawRectangle((int)(x + 6),  (int)(y + 48), 44, 6,  (Color){ 150, 160, 172, 255 });
}


void DrawServerIcon(float x, float y, bool active)
{
    for (int i = 0; i < 3; i++)
    {
        float yy = y + i * 19.0f;

        DrawRectangle((int)(x + 4), (int)yy, 52, 16, deviceDark);
        DrawCircle((int)(x + 13), (int)(yy + 8), 3, active ? green : (Color){ 130, 140, 150, 255 });
        DrawRectangle((int)(x + 24), (int)(yy + 4),  26, 2, (Color){ 110, 122, 138, 255 });
        DrawRectangle((int)(x + 24), (int)(yy + 9),  26, 2, (Color){ 110, 122, 138, 255 });
    }
}


void DrawDeviceBox(float x, float y, bool isServer,
                   const char *name, const char *sub,
                   const char *ip, const char *portText,
                   const char *state, Color stateColor, bool active)
{
    Rectangle r = { x, y, BOX_W, BOX_H };

    DrawRectangleRec(r, panelColor);
    DrawRectangleLinesEx(r, active ? 3 : 2, active ? cyan : darkGray);

    if (isServer)
        DrawServerIcon(x + 14, y + 12, active);
    else
        DrawComputerIcon(x + 14, y + 12, active);

    DrawUIText(name, x + 96, y + 12, 24, textColor);
    DrawFitText(sub, x + 96, y + 44, 140, 15, gray);

    DrawCircle((int)(x + BOX_W - 18), (int)(y + 18), 7, active ? green : darkGray);

    DrawUIText(ip,       x + 14, y + 76,  17, textColor);
    DrawUIText(portText, x + 14, y + 97,  16, gray);

    Rectangle pill = { x + 14, y + 120, BOX_W - 28, 24 };

    DrawRectangleRec(pill, stateColor);
    DrawCenteredText(state, pill, 16, white);
}


/* type: 0 switch, 1 router, 2 internet cloud */
void DrawNetDevice(int type, float cx, float cy, bool hot, const char *label)
{
    float x = cx - 22.0f;
    float y = cy - 13.0f;

    Color led = hot ? orange : (Color){ 150, 170, 190, 255 };

    if (type == 0)
    {
        DrawRectangle((int)x, (int)(y + 5), 44, 16, deviceDark);

        for (int k = 0; k < 4; k++)
            DrawRectangle((int)(x + 5 + k * 9), (int)(y + 10), 6, 6, led);
    }
    else if (type == 1)
    {
        DrawRectangle((int)x, (int)(y + 10), 44, 14, deviceDark);
        DrawLineEx((Vector2){ x + 8,  y + 10 }, (Vector2){ x + 4,  y + 1 }, 2, deviceDark);
        DrawLineEx((Vector2){ x + 36, y + 10 }, (Vector2){ x + 40, y + 1 }, 2, deviceDark);

        for (int k = 0; k < 3; k++)
            DrawCircle((int)(x + 10 + k * 12), (int)(y + 17), 2.5f, led);
    }
    else
    {
        Color cc = hot ? (Color){ 255, 205, 150, 255 } : (Color){ 150, 175, 205, 255 };

        DrawCircle((int)(x + 13), (int)(y + 16), 9,  cc);
        DrawCircle((int)(x + 24), (int)(y + 11), 11, cc);
        DrawCircle((int)(x + 35), (int)(y + 16), 8,  cc);
        DrawRectangle((int)(x + 13), (int)(y + 16), 22, 9, cc);
    }

    if (hot)
        DrawRectangleLinesEx((Rectangle){ x - 5, y - 3, 54, 32 }, 2, orange);

    DrawUIText(label, cx + 30, cy - 9, 15, hot ? orange : gray);
}


/* ---------- packet bar: [ETH][IP][TCP][HTTP][FCS] ---------- */

void DrawBarPart(float x, float y, float w, float h, Color c, const char *label, bool showText)
{
    if (w < 1.0f)
        return;

    Rectangle r = { x, y, w, h };

    DrawRectangleRec(r, c);
    DrawRectangleLinesEx(r, 2, white);

    if (showText && label[0])
    {
        Vector2 m = MeasureTextEx(uiFont, label, 15, 1.0f);

        if (m.x + 6 < w)
            DrawUIText(label, x + (w - m.x) / 2.0f, y + (h - 15) / 2.0f - 1.0f, 15, white);
    }
}


/* amt = number of layers present (1 = HTTP only ... 4 = all layers).
   Fractions animate a layer being added (or removed). */
void DrawPacketBar(float cx, float cy, float amt, float scale, bool showText,
                   const char *dataLabel, Color dataColor, bool secure,
                   const char *caption, bool captionAbove)
{
    float h     = 36.0f * scale;
    float wData = 100.0f * scale;
    float wTcp  = 56.0f * scale;
    float wIp   = 44.0f * scale;
    float wEth  = 54.0f * scale;
    float wFcs  = 46.0f * scale;

    float f1 = ClampF(amt - 1.0f, 0.0f, 1.0f);
    float f2 = ClampF(amt - 2.0f, 0.0f, 1.0f);
    float f3 = ClampF(amt - 3.0f, 0.0f, 1.0f);

    float top    = cy - h / 2.0f;
    float dLeft  = cx - wData / 2.0f;
    float dRight = cx + wData / 2.0f;

    float tcpW = wTcp * f1;
    float ipW  = wIp  * f2;
    float ethW = wEth * f3;
    float fcsW = wFcs * f3;

    float tcpX = dLeft - tcpW;
    float ipX  = tcpX - ipW;
    float ethX = ipX - ethW;

    DrawBarPart(ethX,   top, ethW, h, layerColors[3], "ETH", showText);
    DrawBarPart(ipX,    top, ipW,  h, layerColors[2], "IP",  showText);
    DrawBarPart(tcpX,   top, tcpW, h, layerColors[1], "TCP", showText);
    DrawBarPart(dLeft,  top, wData, h, dataColor, secure ? "" : dataLabel, showText);
    DrawBarPart(dRight, top, fcsW, h, layerColors[3], "FCS", showText);

    if (secure && showText)
    {
        /* little padlock = (simulated) TLS protection */
        float lx = dLeft + 10.0f;
        float ly = cy - 2.0f;

        DrawRectangleLinesEx((Rectangle){ lx + 2, ly - 8, 9, 8 }, 2, white);
        DrawRectangle((int)lx, (int)ly, 13, 10, white);

        DrawUIText(dataLabel, dLeft + 30.0f, cy - 8.0f, 15, white);
    }

    if (caption && caption[0])
    {
        float mid = (ethX + dRight + fcsW) / 2.0f;
        Vector2 m = MeasureTextEx(uiFont, caption, 16, 1.0f);
        float ty = captionAbove ? (top - 24.0f) : (top + h + 6.0f);

        DrawUIText(caption, mid - m.x / 2.0f, ty, 16, textColor);
    }
}


/* speech bubble. side: -1 tail to the client, +1 tail to the server, 0 no tail */
void DrawBubble(Rectangle r, int side, Color accent, const Explain *e)
{
    DrawRectangleRec(r, panelColor);
    DrawRectangleLinesEx(r, 2, accent);

    float mid = r.y + r.height / 2.0f;

    if (side < 0)
    {
        Vector2 tip = { r.x - 26, mid };
        Vector2 a   = { r.x, mid - 14 };
        Vector2 b   = { r.x, mid + 14 };

        DrawTriangle(tip, a, b, panelColor);
        DrawTriangle(tip, b, a, panelColor);
        DrawLineEx(tip, a, 2, accent);
        DrawLineEx(tip, b, 2, accent);
        DrawRectangle((int)r.x - 1, (int)(mid - 13), 4, 27, panelColor);
    }
    else if (side > 0)
    {
        float rx = r.x + r.width;

        Vector2 tip = { rx + 26, mid };
        Vector2 a   = { rx, mid - 14 };
        Vector2 b   = { rx, mid + 14 };

        DrawTriangle(tip, a, b, panelColor);
        DrawTriangle(tip, b, a, panelColor);
        DrawLineEx(tip, a, 2, accent);
        DrawLineEx(tip, b, 2, accent);
        DrawRectangle((int)rx - 3, (int)(mid - 13), 4, 27, panelColor);
    }

    float x = r.x + 18.0f;
    float w = r.width - 36.0f;

    DrawUIText(e->speaker, x, r.y + 8, 15, accent);

    DrawWrapped(e->say, x, r.y + 28, w, 20, 24, textColor, 2);

    const char *labels[4] = { "TECHNICAL:", "LAYER JOB:", "ADDS / DOES:", "NEXT:" };
    const char *texts[4]  = { e->tech, e->job, e->adds, e->next };

    for (int i = 0; i < 4; i++)
    {
        float ly = r.y + 82.0f + i * 21.0f;

        DrawUIText(labels[i], x, ly + 1, 14, accent);
        DrawFitText(texts[i], x + 112, ly, w - 112, 16, gray);
    }
}


/* one of the four layer information boxes */
void DrawLayerBox(Rectangle r, int layer, const char *title, bool filled, bool active,
                  char L[5][128], int count)
{
    Color c = layerColors[layer];

    DrawRectangleRec(r, filled ? panelColor : panelLight);
    DrawRectangleLinesEx(r, active ? 3 : 1, active ? c : darkGray);

    Rectangle hdr = { r.x, r.y, r.width, 28 };

    DrawRectangleRec(hdr, filled ? c : idleGray);
    DrawCenteredText(title, hdr, 16, white);

    for (int i = 0; i < count; i++)
    {
        DrawFitText(L[i], r.x + 12, r.y + 36 + i * 23.0f, r.width - 24, 16,
                    filled ? textColor : placeholder);
    }
}


void DrawProgress(float progress, float x, float y, float w)
{
    Rectangle r = { x, y, w, 16 };

    DrawRectangleRec(r, panelLight);
    DrawRectangle((int)x, (int)y, (int)(w * progress), 16, cyan);
    DrawRectangleLinesEx(r, 1, darkGray);
}


/* =========================================================
   INPUT FIELD BUFFER
   ========================================================= */

char *GetFieldBuffer(HttpRequest *req, int field, int *maxLen)
{
    switch (field)
    {
        case 0:  *maxLen = 95;  return req->host;
        case 1:  *maxLen = 95;  return req->path;
        default: *maxLen = 245; return req->body;
    }
}


/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE |
                   FLAG_WINDOW_MAXIMIZED |
                   FLAG_MSAA_4X_HINT |
                   FLAG_VSYNC_HINT);

    InitWindow(1280, 800, "HTTP / HTTPS Network Simulator");

    SetTargetFPS(60);


    /* ---------- FONT (large size + smooth filter = clear text) ---------- */

    bool customFont = false;

    if (FileExists("C:/Windows/Fonts/segoeui.ttf"))
    {
        uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 40, 0, 0);
        GenTextureMipmaps(&uiFont.texture);
        SetTextureFilter(uiFont.texture, TEXTURE_FILTER_TRILINEAR);
        customFont = true;
    }
    else
    {
        uiFont = GetFontDefault();
    }


    /* ---------- DATA ---------- */

    HttpRequest request = CreateDefaultRequest();

    bool expectSuccess = true;
    int  failCode      = 404;

    HttpResponse response = BuildResponse(&request, 200);

    TCPSegment tcp;
    strcpy(tcp.source, "Client");
    strcpy(tcp.destination, "Server");
    tcp.srcPort = CLIENT_SRC_PORT;
    tcp.port = 443;

    IPPacket packet;
    strcpy(packet.sourceIP, "192.168.1.10");
    strcpy(packet.destinationIP, "192.168.1.20");

    Frame frame;
    strcpy(frame.sourceMAC, "AA:BB:CC:11:22:33");
    strcpy(frame.destinationMAC, "DD:EE:FF:44:55:66");


    /* ---------- SIMULATION STATE ---------- */

    Stage stage = IDLE;
    float stageTimer = 0.0f;

    bool paused   = false;
    bool autoMode = false;     /* default: MANUAL */
    int  speedIdx = 0;         /* default: SLOW   */

    int   activeInput = 0;     /* 0 HOST, 1 PATH, 2 BODY */
    float backTimer = 0.0f;


    /* =====================================================
       MAIN LOOP
       ===================================================== */

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        UpdateUIScale();

        if (IsKeyPressed(KEY_F11))
            ToggleFullscreen();


        bool active  = (stage != IDLE && stage != COMPLETE);
        bool canEdit = !active;
        bool edited  = false;


        /* ---------- TYPING ---------- */

        if (canEdit)
        {
            int maxLen;
            char *buf = GetFieldBuffer(&request, activeInput, &maxLen);

            int key = GetCharPressed();

            while (key > 0)
            {
                if (key >= 32 && key <= 126)
                {
                    int len = (int)strlen(buf);

                    if (len < maxLen)
                    {
                        buf[len] = (char)key;
                        buf[len + 1] = '\0';
                        edited = true;
                    }
                }

                key = GetCharPressed();
            }

            bool erase = false;

            if (IsKeyPressed(KEY_BACKSPACE))
            {
                erase = true;
                backTimer = 0.4f;
            }
            else if (IsKeyDown(KEY_BACKSPACE))
            {
                backTimer -= dt;

                if (backTimer <= 0.0f)
                {
                    erase = true;
                    backTimer = 0.04f;
                }
            }

            if (erase)
            {
                int len = (int)strlen(buf);

                if (len > 0)
                {
                    buf[len - 1] = '\0';
                    edited = true;
                }
            }

            if (IsKeyPressed(KEY_TAB))
                activeInput = (activeInput + 1) % 3;
        }


        /* ---------- MOUSE: REQUEST BUILDER ---------- */

        if (canEdit)
        {
            if (Clicked(inHost))          activeInput = 0;
            else if (Clicked(inPath))     activeInput = 1;
            else if (Clicked(inBody))     activeInput = 2;

            if (Clicked(btnGet))  { strcpy(request.method, "GET");  edited = true; }
            if (Clicked(btnPost)) { strcpy(request.method, "POST"); edited = true; }

            if (Clicked(btnSuccess)) { expectSuccess = true;  edited = true; }
            if (Clicked(btnFailure)) { expectSuccess = false; edited = true; }

            if (!expectSuccess)
            {
                if (Clicked(chip400)) { failCode = 400; edited = true; }
                if (Clicked(chip404)) { failCode = 404; edited = true; }
                if (Clicked(chip500)) { failCode = 500; edited = true; }
            }

            if (Clicked(btnProtocol))
            {
                request.protocol = (request.protocol == HTTPS) ? HTTP : HTTPS;
                edited = true;
            }
        }

        tcp.port = (request.protocol == HTTPS) ? 443 : 80;

        /* changing a setting after a finished run clears the old result */
        if (edited && stage == COMPLETE)
        {
            stage = IDLE;
            stageTimer = 0.0f;
            paused = false;
        }


        /* ---------- MODE / SPEED (allowed at any time) ---------- */

        if (Clicked(btnManual)) autoMode = false;
        if (Clicked(btnAuto))   autoMode = true;

        if (Clicked(btnSlow))   speedIdx = 0;
        if (Clicked(btnNormal)) speedIdx = 1;
        if (Clicked(btnFast))   speedIdx = 2;

        float f = SpeedFactor(speedIdx);


        /* ---------- SIMULATION CONTROLS ---------- */

        active  = (stage != IDLE && stage != COMPLETE);
        canEdit = !active;

        bool doSend  = canEdit && (Clicked(btnSend) || IsKeyPressed(KEY_ENTER));
        bool doPause = active  && (Clicked(btnPause) || IsKeyPressed(KEY_SPACE));
        bool doNext  = (stage != COMPLETE) && (Clicked(btnNext) || (active && IsKeyPressed(KEY_RIGHT)));
        bool doPrev  = (stage > CLIENT_CREATES) && (Clicked(btnPrev) || IsKeyPressed(KEY_LEFT));
        bool doReset = Clicked(btnReset);

        if (doReset)
        {
            stage = IDLE;
            stageTimer = 0.0f;
            paused = false;
        }
        else if (doSend || (doNext && stage == IDLE))
        {
            response = BuildResponse(&request, expectSuccess ? 200 : failCode);
            stage = CLIENT_CREATES;
            stageTimer = 0.0f;
            paused = false;
        }
        else if (doNext)
        {
            stage = (Stage)(stage + 1);
            stageTimer = 0.0f;
        }
        else if (doPrev)
        {
            stage = (Stage)(stage - 1);
            stageTimer = 0.0f;
        }

        if (doPause)
            paused = !paused;


        /* ---------- ANIMATION CLOCK ---------- */

        active = (stage != IDLE && stage != COMPLETE);

        if (stage != IDLE && !paused)
        {
            stageTimer += dt;

            if (active && autoMode &&
                stageTimer >= (AnimDur(stage) + HOLD_TIME) / f)
            {
                stage = (Stage)(stage + 1);
                stageTimer = 0.0f;
            }
        }

        float dur = AnimDur(stage);
        float t = (stage != IDLE) ? ClampF(stageTimer * f / dur, 0.0f, 1.0f) : 0.0f;
        bool  animDone = (stage != IDLE) && (stageTimer * f >= dur);

        bool secure = (request.protocol == HTTPS);


        /* ---------- PACKET STATE FOR THIS FRAME ---------- */

        bool  showPacket = false;
        bool  dataOnly   = false;
        bool  resp       = false;
        bool  showBadge  = false;
        float amt        = 1.0f;
        float scale      = 1.0f;
        float px = ENC_X, py = LANE_T;
        int   boxCount   = 0;
        bool  transit    = false;
        bool  gearOn     = false;

        float e = Ease(t / 0.6f);               /* growth of the new layer */
        float k = (t < 0.12f) ? t / 0.12f : ((t > 0.88f) ? (1.0f - t) / 0.12f : 1.0f);
        float transitScale = 1.0f - 0.4f * k;

        switch (stage)
        {
            case CLIENT_CREATES:
            {
                Vector2 p = PathPoint(reqStart, 3, Ease(t));
                showPacket = true; dataOnly = true; amt = 1.0f;
                px = p.x; py = p.y; boxCount = 0;
                break;
            }
            case HTTP_REQUEST:
                showPacket = true; amt = 1.0f; px = ENC_X; py = LANE_T; boxCount = 1;
                break;
            case TCP_SEGMENT:
                showPacket = true; amt = 1.0f + e; px = ENC_X; py = LANE_T; boxCount = 2;
                break;
            case IP_PACKET:
                showPacket = true; amt = 2.0f + e; px = ENC_X; py = LANE_T; boxCount = 3;
                break;
            case FRAME:
                showPacket = true; amt = 3.0f + e; px = ENC_X; py = LANE_T; boxCount = 4;
                break;
            case NETWORK:
                showPacket = true; amt = 4.0f; transit = true; boxCount = 4;
                px = ENC_X + (DEC_X - ENC_X) * t; py = LANE_T; scale = transitScale;
                break;
            case SERVER_RECEIVES:
                showPacket = true;
                amt = 4.0f - 3.0f * ClampF((t - 0.1f) / 0.7f, 0.0f, 1.0f);
                px = DEC_X; py = LANE_T;
                boxCount = (int)(amt + 0.999f);
                break;
            case SERVER_PROCESSING:
            {
                float u = Ease(t / 0.5f);
                boxCount = 1;
                if (t < 0.55f)
                {
                    showPacket = true; amt = 1.0f;
                    px = DEC_X + (SERVER_CX - DEC_X) * u; py = LANE_T;
                    scale = 1.0f - 0.6f * u;
                }
                else
                {
                    gearOn = true;
                }
                break;
            }
            case HTTP_RESPONSE:
            {
                Vector2 p = PathPoint(respStart, 3, Ease(t / 0.7f));
                showPacket = true; resp = true; amt = 1.0f;
                px = p.x; py = p.y; boxCount = 1;
                break;
            }
            case RESPONSE_TCP:
                showPacket = true; resp = true; amt = 1.0f + e; px = DEC_X; py = LANE_B; boxCount = 2;
                break;
            case RESPONSE_IP:
                showPacket = true; resp = true; amt = 2.0f + e; px = DEC_X; py = LANE_B; boxCount = 3;
                break;
            case RESPONSE_FRAME:
                showPacket = true; resp = true; amt = 3.0f + e; px = DEC_X; py = LANE_B; boxCount = 4;
                break;
            case RESPONSE_NETWORK:
                showPacket = true; resp = true; amt = 4.0f; transit = true; boxCount = 4;
                px = DEC_X + (ENC_X - DEC_X) * t; py = LANE_B; scale = transitScale;
                break;
            case COMPLETE:
                resp = true;
                px = ENC_X; py = LANE_B;
                if (t < 0.8f)
                {
                    showPacket = true;
                    amt = 4.0f - 3.0f * ClampF((t - 0.1f) / 0.6f, 0.0f, 1.0f);
                    boxCount = (int)(amt + 0.999f);
                }
                else
                {
                    showBadge = true;
                    boxCount = 1;
                }
                break;
            default:
                break;
        }

        bool boxesResp = (stage >= HTTP_RESPONSE);

        int activeLayer = ActiveLayer(stage);

        if (stage == SERVER_RECEIVES || stage == COMPLETE)
            activeLayer = ClampI((int)(amt + 0.999f) - 1, 0, 3);

        bool reqRoute  = (stage >= CLIENT_CREATES && stage <= SERVER_PROCESSING);
        bool respRoute = (stage >= HTTP_RESPONSE);


        /* =================================================
           DRAWING
           ================================================= */

        BeginDrawing();

        ClearBackground(bgColor);

        Camera2D cam = { 0 };
        cam.offset = uiOffset;
        cam.zoom = uiScale;
        cam.rotation = 0.0f;

        BeginMode2D(cam);


        /* ---------- HEADER ---------- */

        DrawUIText("HTTP / HTTPS NETWORK SIMULATOR", 40, 8, 32, textColor);

        DrawUIText("Application Layer  ->  Transport Layer  ->  Network Layer  ->  Data Link Layer",
                   40, 44, 16, gray);

        {
            const char *hint = "Keys:  SPACE = pause  |  LEFT / RIGHT = previous / next step  |  ENTER = send  |  TAB = next field  |  F11 = full screen";
            Vector2 hm = MeasureTextEx(uiFont, hint, 14, 1.0f);
            DrawUIText(hint, UI_WIDTH - 40 - hm.x, 46, 14, gray);
        }


        /* ---------- REQUEST BUILDER ---------- */

        DrawPanel(30, 66, 1540, 190, "REQUEST BUILDER");

        DrawUIText("METHOD", btnGet.x, btnGet.y - 20, 15, gray);
        DrawToggle("GET",  btnGet,  blue, strcmp(request.method, "GET")  == 0, canEdit);
        DrawToggle("POST", btnPost, blue, strcmp(request.method, "POST") == 0, canEdit);
        DrawUIText("GET = read data, POST = send data", btnGet.x, btnGet.y + btnGet.height + 4, 14, gray);

        DrawInputBox("HOST / SERVER NAME", request.host, "e.g. www.google.com",
                     "Which server the client wants to contact.",
                     inHost, activeInput == 0 && canEdit, canEdit);

        DrawInputBox("REQUEST PATH (URI)", request.path, "e.g. /users",
                     "Which resource the client wants from that server.",
                     inPath, activeInput == 1 && canEdit, canEdit);

        DrawInputBox("REQUEST BODY (PAYLOAD) - optional", request.body, "(empty)",
                     "The data sent to the server, normally with POST. Leave empty for GET.",
                     inBody, activeInput == 2 && canEdit, canEdit);


        /* protocol */

        DrawUIText("PROTOCOL", btnProtocol.x, btnProtocol.y - 20, 15, gray);

        DrawButton(secure ? "HTTPS" : "HTTP", btnProtocol, secure ? green : orange, canEdit);

        DrawUIText(secure ? "TLS 1.3 simulated  |  port 443"
                          : "No TLS, plain text  |  port 80",
                   btnProtocol.x + btnProtocol.width + 14,
                   btnProtocol.y + 10, 16,
                   secure ? green : orange);


        /* server response selection */

        DrawUIText("SERVER RESPONSE", btnSuccess.x, btnSuccess.y - 20, 15, gray);

        DrawToggle("SUCCESS", btnSuccess, green, expectSuccess,  canEdit);
        DrawToggle("FAILURE", btnFailure, red,   !expectSuccess, canEdit);

        if (expectSuccess)
        {
            DrawUIText("-> 200 OK", chip400.x, chip400.y + 9, 20, green);
        }
        else
        {
            DrawToggle("400", chip400, red, failCode == 400, canEdit);
            DrawToggle("404", chip404, red, failCode == 404, canEdit);
            DrawToggle("500", chip500, red, failCode == 500, canEdit);
        }

        {
            HttpResponse preview = BuildResponse(&request, expectSuccess ? 200 : failCode);
            char pt[260];

            snprintf(pt, sizeof(pt), "%d %s:  %s", preview.statusCode, preview.statusText, preview.explain);

            DrawFitText(pt, 970, btnSuccess.y + 11, 580, 16, expectSuccess ? green : red);
        }


        /* ---------- CONTROL BAR ---------- */

        {
            Rectangle bar = { 30, 262, 1540, 44 };

            DrawRectangleRec(bar, panelColor);
            DrawRectangleLinesEx(bar, 1, darkGray);

            const char *sendLabel = active ? "RUNNING..." : (stage == COMPLETE ? "SEND AGAIN" : "SEND REQUEST");

            DrawButton(sendLabel, btnSend, green, canEdit);
            DrawButton(paused ? "RESUME" : "PAUSE", btnPause, orange, active);
            DrawButton("< PREVIOUS", btnPrev, blue, stage > CLIENT_CREATES);
            DrawButton("NEXT STEP >", btnNext, blue, stage != COMPLETE);
            DrawButton("RESET", btnReset, red, true);

            DrawUIText("MODE:", 775, 274, 16, gray);
            DrawToggle("MANUAL", btnManual, cyan, !autoMode, true);
            DrawToggle("AUTO",   btnAuto,   cyan, autoMode,  true);

            DrawUIText("SPEED:", 1048, 274, 16, gray);
            DrawToggle("SLOW",   btnSlow,   blue, speedIdx == 0, true);
            DrawToggle("NORMAL", btnNormal, blue, speedIdx == 1, true);
            DrawToggle("FAST",   btnFast,   blue, speedIdx == 2, true);

            char stepText[40];

            if (stage == IDLE)
                snprintf(stepText, sizeof(stepText), "Step - / %d", STEP_COUNT);
            else
                snprintf(stepText, sizeof(stepText), "Step %d / %d", (int)stage - (int)CLIENT_CREATES + 1, STEP_COUNT);

            DrawUIText(stepText, 1405, 274, 16, textColor);
        }


        /* ---------- NETWORK TRANSMISSION ---------- */

        DrawPanel(30, 316, 1540, 330, "NETWORK TRANSMISSION");

        /* legend */
        DrawRectangle(1230, 327, 14, 14, blue);
        DrawUIText("REQUEST", 1250, 322, 15, gray);
        DrawRectangle(1340, 327, 14, 14, purple);
        DrawUIText("RESPONSE", 1360, 322, 15, gray);

        DrawRoute(reqRoute, respRoute);

        /* network devices along both lanes */
        {
            const char *names[4]  = { "Switch", "Router", "Internet", "Router" };
            int         types[4]  = { 0, 1, 2, 1 };
            float       devX[4]   = { 520.0f, 670.0f, 820.0f, 970.0f };

            for (int i = 0; i < 4; i++)
            {
                bool hotTop = transit && stage == NETWORK          && AbsF(px - devX[i]) < 45.0f;
                bool hotBot = transit && stage == RESPONSE_NETWORK && AbsF(px - devX[i]) < 45.0f;

                DrawNetDevice(types[i], devX[i], 338.0f, hotTop, names[i]);
                DrawNetDevice(types[3 - i], devX[3 - i], 630.0f, hotBot, names[3 - i]);
            }
        }


        /* client / server boxes */

        bool clientActive = (stage >= CLIENT_CREATES && stage <= FRAME) || stage == COMPLETE;
        bool serverActive = (stage >= SERVER_RECEIVES && stage <= RESPONSE_FRAME);

        const char *clientState = "Idle";
        Color clientColor = idleGray;

        if (stage >= CLIENT_CREATES && stage <= IP_PACKET)      { clientState = "Creating Request";     clientColor = blue; }
        else if (stage == FRAME || stage == NETWORK)            { clientState = "Sending Packet";       clientColor = cyan; }
        else if (stage >= SERVER_RECEIVES && stage <= RESPONSE_NETWORK) { clientState = "Waiting for Response"; clientColor = orange; }
        else if (stage == COMPLETE)                             { clientState = "Response Received";    clientColor = (response.statusCode < 400) ? green : red; }

        const char *serverState = "Idle";
        Color serverColor = idleGray;

        if (stage == NETWORK || stage == SERVER_RECEIVES)       { serverState = "Receiving Packet";     serverColor = cyan; }
        else if (stage == SERVER_PROCESSING)                    { serverState = "Processing Request";   serverColor = orange; }
        else if (stage >= HTTP_RESPONSE && stage <= RESPONSE_NETWORK) { serverState = "Sending Response"; serverColor = blue; }
        else if (stage == COMPLETE)                             { serverState = "Response Sent";        serverColor = green; }

        char hostShort[40];
        snprintf(hostShort, sizeof(hostShort), "%.17s%s",
                 request.host[0] ? request.host : "(no host)",
                 strlen(request.host) > 17 ? ".." : "");

        char clientIP[64], clientPort[64], serverIP[64], serverPort[64];

        snprintf(clientIP,   sizeof(clientIP),   "IP: %s", packet.sourceIP);
        snprintf(clientPort, sizeof(clientPort), "Source port: %d", tcp.srcPort);
        snprintf(serverIP,   sizeof(serverIP),   "IP: %s", packet.destinationIP);
        snprintf(serverPort, sizeof(serverPort), "Dest. port: %d (%s)", tcp.port, secure ? "HTTPS" : "HTTP");

        DrawDeviceBox(CLIENT_BOX_X, BOX_Y, false, "CLIENT", "Web browser",
                      clientIP, clientPort, clientState, clientColor, clientActive);

        DrawDeviceBox(SERVER_BOX_X, BOX_Y, true, "SERVER", hostShort,
                      serverIP, serverPort, serverState, serverColor, serverActive);


        /* speech bubble (explanation) */

        {
            Explain ex;
            BuildExplain(stage, &request, &response, &tcp, &packet, &frame, &ex);

            int side = 0;

            if ((stage >= CLIENT_CREATES && stage <= FRAME) || stage == COMPLETE)
                side = -1;
            else if (stage >= SERVER_RECEIVES && stage <= RESPONSE_FRAME)
                side = 1;

            Color accent = cyan;

            if (stage != NETWORK && stage != RESPONSE_NETWORK && activeLayer >= 0)
                accent = layerColors[activeLayer];

            DrawBubble((Rectangle){ 330, 398, 940, 172 }, side, accent, &ex);
        }


        /* server gear while processing */

        if (gearOn)
        {
            float ang = stageTimer * 160.0f;

            DrawPoly((Vector2){ SERVER_CX, 394.0f }, 8, 13.0f, ang, orange);
            DrawPoly((Vector2){ SERVER_CX, 394.0f }, 8, 13.0f, ang + 22.5f, orange);
            DrawCircle((int)SERVER_CX, 394, 5, white);
        }


        /* the packet itself */

        if (showPacket)
        {
            bool showText = (scale >= 0.9f);

            DrawPacketBar(px, py, amt, scale, showText,
                          dataOnly ? "DATA" : (secure ? "HTTPS" : "HTTP"),
                          dataOnly ? dataGray : layerColors[0],
                          secure && !dataOnly,
                          showText ? ((stage == SERVER_RECEIVES || stage == COMPLETE) ? "UNWRAPPING LAYERS"
                                                                                    : PacketCaption(stage)) : "",
                          !resp);
        }

        if (showBadge)
        {
            Color badge = (response.statusCode < 400) ? green : red;
            char code[80];

            snprintf(code, sizeof(code), "%d %s", response.statusCode, response.statusText);

            Rectangle br = { ENC_X - 105, LANE_B - 22, 210, 44 };

            DrawRectangleRec(br, badge);
            DrawRectangleLinesEx(br, 2, white);
            DrawCenteredText(code, br, 18, white);

            const char *cap = "RESPONSE RECEIVED";
            Vector2 cm = MeasureTextEx(uiFont, cap, 16, 1.0f);
            DrawUIText(cap, ENC_X - cm.x / 2.0f, LANE_B + 26, 16, textColor);
        }


        /* ---------- STAGE / PROGRESS / STATUS ---------- */

        {
            float progress = 0.0f;

            if (stage == COMPLETE)
                progress = 1.0f;
            else if (stage != IDLE)
                progress = ((float)((int)stage - (int)CLIENT_CREATES) + t) / (float)(STEP_COUNT - 1);

            DrawUIText("CURRENT STAGE", 50, 650, 14, gray);
            DrawFitText(GetStageName(stage), 50, 666, 470, 20, cyan);

            DrawUIText("TRANSACTION PROGRESS", 560, 650, 14, gray);
            DrawProgress(progress, 560, 670, 500);

            if (paused)
                DrawUIText("PAUSED", 1075, 667, 18, orange);

            DrawUIText("STATUS", 1190, 650, 14, gray);

            if (stage == IDLE)
            {
                DrawUIText("Waiting for request...", 1190, 666, 20, gray);
            }
            else if (stage == COMPLETE)
            {
                char st[100];

                snprintf(st, sizeof(st), "%s - %d %s",
                         response.statusCode < 400 ? "SUCCESS" : "FAILED",
                         response.statusCode, response.statusText);

                DrawUIText(st, 1190, 666, 20, response.statusCode < 400 ? green : red);
            }
            else if (paused)
            {
                DrawUIText("Paused", 1190, 666, 20, orange);
            }
            else if (!autoMode && animDone)
            {
                DrawUIText("Press NEXT STEP", 1190, 666, 20, blue);
            }
            else if (stage == SERVER_PROCESSING)
            {
                DrawUIText("Processing...", 1190, 666, 20, orange);
            }
            else
            {
                DrawUIText("Transmitting...", 1190, 666, 20, cyan);
            }
        }


        /* ---------- PACKET INFORMATION ---------- */

        DrawPanel(30, 692, 1540, 204, "PACKET INFORMATION");

        {
            const char *note = "Simulated values - no real packets are sent over the internet.";
            Vector2 nm = MeasureTextEx(uiFont, note, 15, 1.0f);
            DrawUIText(note, 1570 - 20 - nm.x, 700, 15, gray);

            char titles[4][64];

            snprintf(titles[0], 64, "%s", secure ? "APPLICATION - HTTPS (TLS simulated)" : "APPLICATION - HTTP (no TLS)");
            snprintf(titles[1], 64, "TRANSPORT - TCP");
            snprintf(titles[2], 64, "NETWORK - IP");
            snprintf(titles[3], 64, "DATA LINK - ETHERNET");

            for (int i = 0; i < 4; i++)
            {
                Rectangle br = { 50.0f + i * 378.0f, 726, 364, 156 };

                char L[5][128];
                int count;

                bool filled = (i < boxCount);

                if (filled || i == 0)
                {
                    bool respValues = boxesResp;
                    count = BuildLayerLines(i, respValues, &request, &response, &tcp, &packet, &frame, L);
                }
                else
                {
                    count = BuildLayerHints(i, L);
                }

                char title[96];

                if (i == 0 && !filled)
                    snprintf(title, sizeof(title), "%s  [preview]", titles[0]);
                else
                    snprintf(title, sizeof(title), "%s", titles[i]);

                DrawLayerBox(br, i, title, filled, (i == activeLayer), L, count);
            }
        }


        EndMode2D();

        EndDrawing();
    }


    /* ---------- CLEANUP ---------- */

    if (customFont)
        UnloadFont(uiFont);

    CloseWindow();

    return 0;
}
