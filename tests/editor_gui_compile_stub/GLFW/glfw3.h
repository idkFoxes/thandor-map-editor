#pragma once
#define GLFW_SCALE_TO_MONITOR 0x0002200C
struct GLFWwindow {};
#define GLFW_FALSE 0
#define GLFW_TRUE 1
inline double glfwGetTime(){ return 0.0; }
inline void glfwPollEvents(){}
inline void glfwSetCursorPos(GLFWwindow*, double, double){}
inline void glfwSetWindowShouldClose(GLFWwindow*, int){}
inline int glfwWindowShouldClose(GLFWwindow*){ return 0; }
