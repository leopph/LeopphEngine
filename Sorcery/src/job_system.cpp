#include "job_system.hpp"

#include <emmintrin.h>

thread_local unsigned this_thread_idx;


namespace sorcery {
JobSystem::JobSystem() {
  auto const thread_count{std::jthread::hardware_concurrency()};
  job_queues_.resize(thread_count);

  auto const worker_count{thread_count - 1};
  workers_.reserve(worker_count);

  for (unsigned i{0}; i < worker_count; i++) {
    workers_.emplace_back([this](std::stop_token const& stop_token, unsigned const thread_idx) {
      this_thread_idx = thread_idx;

      while (!stop_token.stop_requested()) {
        if (auto const job{FindJobToExecute()}) {
          Execute(*job);
        } else {
          _mm_pause();
        }
      }
    }, i + 1);
  }
}


JobSystem::~JobSystem() {
  for (auto& worker : workers_) {
    worker.request_stop();
  }
}


auto JobSystem::CreateJob(JobFuncType func) -> Job* {
  auto const job_idx{(next_job_idx_++) % jobs_.size()};
  auto const job{&jobs_[job_idx]};
  job->func = func;
  job->is_complete = false;
  return job;
}


auto JobSystem::Run(Job* const job) -> void {
  auto& [jobs, mutex]{job_queues_[this_thread_idx]};
  std::scoped_lock lock{*mutex};
  jobs.push(job);
}


auto JobSystem::Wait(Job const* job) -> void {
  while (!job->is_complete) {
    if (auto const new_job{FindJobToExecute()}) {
      Execute(*new_job);
    }
  }
}


auto JobSystem::Execute(Job& job) -> void {
  job.func(job.data.data());
  job.is_complete = true;
}


auto JobSystem::FindJobToExecute() -> Job* {
  Job* job{nullptr};

  auto const try_get_job_from_queue_at_idx{
    [this, &job](unsigned const queue_idx) {
      auto& [jobs, mutex]{job_queues_[queue_idx]};
      std::scoped_lock lock{*mutex};

      if (!jobs.empty()) {
        job = jobs.front();
        jobs.pop();
        return true;
      }

      return false;
    }
  };

  if (!try_get_job_from_queue_at_idx(this_thread_idx)) {
    for (unsigned j{0}; j < workers_.size() + 1; j++) {
      if (j != this_thread_idx && try_get_job_from_queue_at_idx(j)) {
        break;
      }
    }
  }

  return job;
}
}
