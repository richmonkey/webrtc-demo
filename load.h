#ifndef VOIP_LOAD_H
#define VOIP_LOAD_H
#include <string>
#include <vector>

void SetCameraLoad(std::string& camera_id, int load);
int GetCameraLoad(std::string& camera_id);

void SetCameraList(std::vector<std::string>& cameras);
std::vector<std::string> GetCameraList();


#endif
