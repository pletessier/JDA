#include <cstdio>
#include <cstring>

using namespace std;

void train();
void resume();
void live();
void fddb();

/*! \breif command help */
static const char help[] = "Joint Cascade Face Detection and Alignment\n\n"
                           "train:  train JDA classifier and regressor for face detection\n"
                           "        and face alignemnt\n"
                           "resume: resume a previous training status\n"
                           "live:   live demo with camera support\n"
                           "fddb:   detection over fddb\n\n";

/*!
 * \breif Command Dispatch
 */
int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf(help);
  }
  else if (strcmp(argv[1], "train") == 0) {
    train();
  }
  else if (strcmp(argv[1], "resume") == 0) {
    resume();
  }
  else if (strcmp(argv[1], "live") == 0) {
    live();
  }
  else if (strcmp(argv[1], "fddb") == 0) {
    fddb();
  }
  else {
    printf(help);
  }
  return 0;
}
