//
//  async.hpp
//  fibio
//
//  Created by Chen Xu on 14-3-29.
//  Copyright (c) 2014 0d0a.com. All rights reserved.
//

#ifndef fibio_fibers_future_async_hpp
#define fibio_fibers_future_async_hpp

#include <algorithm>
#include <memory>
#include <tuple>
#include <fibio/utility.hpp>
#include <fibio/fibers/fiber.hpp>
#include <fibio/fibers/future/future.hpp>
#include <fibio/fibers/future/packaged_task.hpp>
#include <fibio/concurrent/concurrent_queue.hpp>

namespace fibio { namespace fibers {
    namespace detail {
        template<typename Fn, class... Args>
        struct task_data {
            typedef typename std::result_of<Fn(Args...)>::type result_type;
            typedef future<result_type> future_type;
            typedef packaged_task<result_type()> task_type;
            task_data(Fn&& fn, Args&&... args)
            : fn_(std::forward<Fn>(fn), std::forward<Args>(args)...)
            {}
            
            template <std::size_t... Indices>
            result_type run2(utility::tuple_indices<Indices...>)
            { return utility::invoke(std::move(std::get<0>(fn_)), std::move(std::get<Indices>(fn_))...); }
            
            result_type operator()() {
                typedef typename utility::make_tuple_indices<std::tuple_size<std::tuple<Fn, Args...> >::value, 1>::type index_type;
                return run2(index_type());
            }
            std::tuple<typename std::decay<Fn>::type, typename std::decay<Args>::type...> fn_;
        };
    }
    
    template<typename Fn, typename ...Args>
    typename detail::task_data<Fn, Args...>::future_type
    async(Fn &&fn, Args&&... args) {
        typedef detail::task_data<Fn, Args...> data;
        typename data::task_type task(data(std::forward<Fn>(fn), std::forward<Args>(args)...));
        typename data::future_type ret(task.get_future());
        fiber(std::move(task)).detach();
        return std::move(ret);
    }
    
    template<typename T>
    struct async_executor {
        async_executor(size_t pool_size=0) {
            if (pool_size==0) pool_size=std::min(this_fiber::get_scheduler().worker_pool_size(),
                                                 size_t(std::thread::hardware_concurrency()));
            for(size_t i=0; i<pool_size; i++) fibers_.emplace_back(&async_executor::execute, this);
        }
        
        ~async_executor()
        { queue_.close(); for(auto &f : fibers_) f.join(); }
        
        template<typename Fn, typename ...Args>
        future<T> operator()(Fn &&fn, Args&&... args) {
            typedef detail::task_data<Fn, Args...> task_type;
            static_assert(std::is_convertible<typename task_type::result_type, T>::value,
                          "function must return compatible type");
            packaged_task<T()> task(task_type(std::forward<Fn>(fn), std::forward<Args>(args)...));
            future<T> ret=task.get_future();
            queue_.push(std::move(task));
            return ret;
        }
        
    private:
        void execute() { for(auto & i: queue_) i(); }
        concurrent::concurrent_queue<packaged_task<T()>> queue_;
        std::vector<fiber> fibers_;
    };
    
    template<typename Fn>
    struct async_function {
        typedef utility::function_traits<Fn> traits_type;
        typedef typename traits_type::result_type result_type;
        typedef typename traits_type::arguments_tuple arguments_tuple;
        
        async_function(Fn &&fn)
        : fn_(std::forward<Fn>(fn))
        , queue_(new concurrent::concurrent_queue<queue_element>)
        , fiber_(&async_function::execute, this)
        {}
        async_function(async_function &&)=default;
        ~async_function() { queue_->close(); fiber_.join(); }
        
        template<typename ...Args>
        future<result_type> operator()(Args&&... args)
        { return async_call(std::make_tuple(std::forward<Args>(args)...)); }
        
        future<result_type> apply(arguments_tuple &&args)
        { return async_call(std::forward<arguments_tuple>(args)); }

    private:
        struct async_function_args{
            arguments_tuple args;
            promise<result_type> ret;
        };
        typedef std::unique_ptr<async_function_args> queue_element;
        
        future<result_type> async_call(arguments_tuple &&args) {
            queue_element e(new async_function_args{std::forward<arguments_tuple>(args), promise<result_type>()});
            future<result_type> ret(e->ret.get_future());
            queue_->push(std::move(e));
            return ret;
        }
        
        template <std::size_t... Indices>
        result_type call2(arguments_tuple &&args, utility::tuple_indices<Indices...>)
        { return utility::invoke(fn_, std::move(std::get<Indices>(args))...); }
        
        void call(queue_element &&args) {
            typedef typename utility::make_tuple_indices<traits_type::arity>::type index_type;
            args->ret.set_value(call2(std::move(args->args), index_type()));
        }
        
        void execute() { for(auto &e : *queue_) call(std::move(e)); }
        
        Fn fn_;
        std::unique_ptr<concurrent::concurrent_queue<queue_element>> queue_;
        fiber fiber_;
    };
    
    template<typename Fn>
    async_function<Fn> make_async(Fn &&fn)
    { return async_function<Fn>(std::forward<Fn>(fn)); }
}}  // End of namespace fibio::fibers

namespace fibio {
    using fibers::async;
    using fibers::async_executor;
    using fibers::async_function;
    using fibers::make_async;
}

#endif
