#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>


namespace sorcery {
using JobFuncType = void(*)(void const* data);
constexpr auto kMaxJobDataSize{55};


struct Job {
  JobFuncType func;
  std::array<char, kMaxJobDataSize> data;
  std::atomic_bool is_complete;
};


static_assert(sizeof(Job) == 64);


class JobSystem {
public:
  JobSystem();

  JobSystem(JobSystem const&) = delete;
  JobSystem(JobSystem&&) = delete;

  ~JobSystem();

  auto operator=(JobSystem const&) -> void = delete;
  auto operator=(JobSystem&&) -> void = delete;

  template<typename T> requires(sizeof(T) <= kMaxJobDataSize && std::is_trivially_copy_constructible_v<T> &&
                                std::is_trivially_destructible_v<T>)
  [[nodiscard]] auto CreateJob(JobFuncType func, T const& data) -> Job*;
  [[nodiscard]] auto CreateJob(JobFuncType func) -> Job*;

  template<typename T>
  [[nodiscard]] auto CreateParallelForJob(void (*func)(T& data), std::span<T> data) -> Job*;

  auto Run(Job* job) -> void;

  auto Wait(Job const* job) -> void;

private:
  struct JobQueue {
    std::queue<Job*> jobs;
    std::unique_ptr<std::mutex> mutex{std::make_unique<std::mutex>()};
  };


  static auto Execute(Job& job) -> void;

  [[nodiscard]] auto FindJobToExecute() -> Job*;

  std::atomic_uintmax_t next_job_idx_{0};
  std::array<Job, 4096> jobs_{};
  std::vector<JobQueue> job_queues_;
  std::vector<std::jthread> workers_;
};
}


#include "job_system.inl"
