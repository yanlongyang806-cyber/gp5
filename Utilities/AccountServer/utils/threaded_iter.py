import threading
import Queue
import sys


class IterThread(threading.Thread):

    def __init__(self, queue, queue_out, func):
        super(IterThread, self).__init__()
        self.queue = queue
        self.queue_out = queue_out
        self.func = func

    def run(self):
        while not self.queue.empty():
            try:
                cur_args = self.queue.get()
            except Empty:
                break

            try:
                self.queue_out.put(self.func(*cur_args))
            except Exception as e:
                e.exc_info = sys.exc_info()
                self.queue_out.put(e)
            else:
                self.queue.task_done()


def threaded_iter(func, args, num_threads=1):
    queue = Queue.Queue()
    num_items = 0
    for arg in args:
        queue.put(arg)
        num_items += 1

    queue_out = Queue.Queue()
    worker_threads = []
    for i in range(num_threads):
        worker = IterThread(queue, queue_out, func)
        worker.setDaemon(True)
        worker_threads.append(worker)

    for worker in worker_threads:
        worker.start()

    while num_items > 0:
        result = queue_out.get(block=True)
        if isinstance(result, Exception):
            raise result.exc_info[1], None, result.exc_info[2]
        yield result
        num_items -= 1;

    queue.join()
