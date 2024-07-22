#include <asm-generic/ioctls.h>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <stdio.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <termios.h>
#include <vector>
#include <mutex>

const char CURSOR = 'X';
const char ACTIVE = '#';
const std::string HIDE_CURSOR = "\033[?25l";
const std::string SHOW_CURSOR = "\033[?25h";

  std::mutex mtx;
  std::condition_variable cv;
  bool lock_av = true;

struct GameScreen
{
  int CursorX;
  int CursorY;
  int Width;
  int Height;
  std::vector<std::vector<char>> Rows ; 
};

void acquire(){
  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [] {return lock_av;});
  lock_av = false;
}

void release(){
  std::lock_guard<std::mutex> lock(mtx);
  lock_av = true;
  cv.notify_one();
}

struct pos {
   int x;
   int y;
};

winsize GetScreenSize()
{
  std::vector<int> v;
  struct winsize w;
  ioctl(STDIN_FILENO, TIOCGWINSZ, &w);
  return w;
}

bool initial = true;
void Render(GameScreen* G){
  std::cout << HIDE_CURSOR;

  GameScreen g = *G;

  if(initial){
    std::vector<char> buffer(g.Width*g.Height);

    for(int y = 0; y < g.Rows.size(); y++){
      for(int x = 0; x < g.Rows[y].size(); x++){
        if(g.CursorY == y && g.CursorX == x){
          buffer.push_back(CURSOR);
        } else {
          buffer.push_back(g.Rows[y][x]);
        }
          buffer.push_back('|');
      }
      for(int x = 0; x < g.Rows[y].size() * 2; x++){
        buffer.push_back('-');
      }
      buffer.push_back('\n');
      
    }
    system("clear");
    for(int i = 0; i < buffer.size(); i++){
      std::cout << buffer[i];
    }
    initial = false;
  } else {

    for(int y = 0; y < g.Rows.size(); y++){
      for(int x = 0; x < g.Rows[y].size(); x++){
        if(g.CursorY == y && g.CursorX == x){
          std::cout << "\033[" << (y==0? 0 : (y*2)+1) << ";" << (x==0? 0 : (x*2)+1) << "H";
          std::cout << CURSOR;
        } else {
          std::cout << "\033[" << (y==0? 0 : (y*2)+1) << ";" << (x==0? 0 : (x*2)+1) << "H";
          std::cout << g.Rows[y][x];
        }
      }
    }

  }
}

struct termios orig_termios;
void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void MoveCursor(GameScreen* g, int xo, int yo){

  if(xo > 0){
    g->CursorX = g->CursorX + xo > g->Width? g->Width : g->CursorX + xo;
  } else {
    g->CursorX = g->CursorX + xo < 0? 0 : g->CursorX + xo;
  }

  if(yo > 0){
    g->CursorY = g->CursorY + yo > g->Height? g->Height: g->CursorY + yo;
  } else {
    g->CursorY = g->CursorY + yo < 0? 0 : g->CursorY + yo;
  }
}

void SetSquare(GameScreen* g){
  char state = g->Rows[g->CursorY][g->CursorX];
  g->Rows[g->CursorY][g->CursorX] = state == ' '? ACTIVE : ' ';
}

int CountNeighbours(GameScreen* g, int x, int y){
  pos offsets[] = {
    {-1, -1,},
    {0, -1},
    {1, -1},
    {-1,0},
    {1,0},
    {-1,1},
    {0,1},
    {1,1}
  };

    int neighours = 0;
    
    for(int p = 0; p < 8; p++){
      pos pos = offsets[p];
      if(g->Rows.size()-1 < y + pos.y || (y + pos.y) < 0){
        continue;
      }
      if(g->Rows[y + pos.y].size()-1 < x + pos.x || (x + pos.x) < 0){
        continue;
      }
      
      if(g->Rows[y + pos.y][x + pos.x] == ACTIVE){
        neighours++;
      }
    }
    return neighours;
}

void Simulate(GameScreen* g){

  std::vector<std::vector<char>> buffer(g->Rows);

  for(int y = 0; y < g->Rows.size(); y++){
    for(int x = 0; x < g->Rows[0].size(); x++){
        int neighours = CountNeighbours(g, x, y);
        if(neighours == 0 || neighours == 1){
          buffer[y][x] = ' ';
        } else if(neighours == 3){
          buffer[y][x] = ACTIVE;
        } else if(neighours >= 4){
          buffer[y][x] = ' ';
        }
    }
  }
  g->Rows = buffer;
  return;
}

bool running = true;
bool simulating = false;

void render_t(GameScreen *g){
  while(running){
    while(simulating){
      acquire();
      Simulate(g);
      Render(g);
      release();
      usleep(7000);
      continue;
    }
  }
}

int main(int argc, char **argv)
{
  winsize w = GetScreenSize();

  printf("lines %d\n", w.ws_row);
  printf("rows %d\n", w.ws_col);

  w.ws_col = w.ws_col/2;
  w.ws_row = w.ws_row/2;
  
  GameScreen g;
  g.Width = w.ws_col;
  g.Height = w.ws_row;
  g.Rows = {};
  g.CursorX = 0;
  g.CursorY = 0;

  for(int y = 0; y < g.Height; y++){
    g.Rows.push_back({});
      g.Rows[y] = {};
    for(int x = 0; x < g.Width; x++){
      g.Rows[y].push_back(x % 2 == 0? ' ' : ' ');
    }
  }

  enableRawMode();
  Render(&g);

  std::thread renderT(render_t, &g);

  char c;
  while ((read(STDIN_FILENO, &c, 1) == 1) && c != 'q') {

    if(simulating){
      if(c == 'x'){
        simulating = false;
        continue;
      }
    }

    switch (c) {
      case 'h':
        MoveCursor(&g, -1, 0);
        break;
      case 'l':
        MoveCursor(&g, 1, 0);
        break;
      case 'j':
        MoveCursor(&g, 0, 1);
        break;
      case 'k':
        MoveCursor(&g, 0, -1);
        break;
      case 32:
        //space
        SetSquare(&g);
        break;
      case '\n':
        simulating = true;
        break;
    }
    acquire();
    Render(&g);
    release();
  } 
  running = false;
  simulating = false;
  renderT.join();
  std::cout << SHOW_CURSOR;
  system("clear");
}

