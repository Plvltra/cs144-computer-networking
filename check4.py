import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import numpy as np
import sys
import re
import os
from datetime import datetime, timedelta

# # 1. 准备数据
# # 生成随机数据点
# np.random.seed(42)  # 设置随机种子确保结果可复现
# x = np.random.rand(50)  # 50个0-1之间的随机x值
# y = np.random.rand(50)  # 50个0-1之间的随机y值
# colors = np.random.rand(50)  # 随机颜色值
# sizes = 1000 * np.random.rand(50)  # 随机大小值

# # 2. 创建图形和坐标轴
# plt.figure(figsize=(8, 6))  # 设置图形大小

# # 3. 绘制散点图
# plt.scatter(x, y, 
#             c=colors,  # 点颜色
#             s=sizes,   # 点大小
#             alpha=0.6, # 透明度
#             cmap='viridis')  # 颜色映射

# # 4. 添加标题和标签
# plt.title('Simple Scatter Plot Example', fontsize=14)
# plt.xlabel('X-axis', fontsize=12)
# plt.ylabel('Y-axis', fontsize=12)

# # 5. 添加颜色条
# plt.colorbar(label='Color Intensity')

# # 6. 显示图形
# plt.show()

MAX_SEQ = 5 * 3600 + 1

if __name__ == '__main__':
    args = sys.argv[1:]
    file_path = args[0]
    dataname = os.path.splitext(os.path.basename(file_path))[0]
    print(f"Processing file: {file_path}")
    os.makedirs(f"./result/{dataname}", exist_ok=True)

    ping_data = {}

    def read_large_file(file_path):
        with open(file_path, 'r', encoding='utf-8') as file:
            for line in file:
                yield line.strip()
    for line in read_large_file(file_path):
        pattern = r'(\d+\.\d+).*icmp_seq=(\d+).*time=(\d+)'
        match = re.search(pattern, line)
        if not match:
            continue
        timestamp = match.group(1)
        icmp_seq = match.group(2)
        time = match.group(3)
        ping_data[int(icmp_seq)] = {
            'timestamp': float(timestamp),
            'rtt': int(time)
        }

    # 1. 发送报文数和接收报文数, 计算总交付率
    print(f"发送报文数: {MAX_SEQ}")
    print(f"接收报文数: {len(ping_data)}")
    print(f"总交付率: {len(ping_data) / MAX_SEQ * 100:.2f}%")

    # 2. 最长连续响应报文数量
    max_consec_received = 0
    max_consec_losses = 0
    consec_received = 0
    consec_losses = 0
    for i in range(1, MAX_SEQ):
        if i not in ping_data:
            consec_losses += 1
            if consec_received > max_consec_received:
                max_consec_received = consec_received
            consec_received = 0
        else:
            consec_received += 1
            if consec_losses > max_consec_losses:
                max_consec_losses = consec_losses
            consec_losses = 0
    print(f"最长连续丢失报文数量: {max_consec_losses}")

    # 3. 最长连续丢失报文数量
    print(f"最长连续响应报文数量: {max_consec_received}")

    # 4.
    received_counts = {}
    received_total = 0
    lost_counts = {}
    lost_total = 0
    for i in range(1 + 10, MAX_SEQ - 10):
        if i not in ping_data:
            lost_total += 1
            for j in range(-10, 10 + 1):
                if i + j not in ping_data:
                    lost_counts[j] = lost_counts.get(j, 0) + 1
        else:
            received_total += 1
            for j in range(-10, 10 + 1):
                if i + j in ping_data:
                    received_counts[j] = received_counts.get(j, 0) + 1
    sorted_received = dict(sorted(received_counts.items()))
    sorted_lost = dict(sorted(lost_counts.items()))
    sorted_received.pop(0, None)
    sorted_lost.pop(0, None)
    received_probs = {k: v / received_total for k, v in sorted_received.items()}
    lost_probs = {k: v / lost_total for k, v in sorted_lost.items()}

    # 4.1 假设#N报文成功收到, 画柱状图[N-10, N+10]成功收到的概率(0-1之间)
    fig, ax = plt.subplots(figsize=(10, 8))  # 创建画布和坐标轴
    ax.plot(received_probs.keys(),
            received_probs.values(),
            label='The proportion of received packets (%)')
    plt.title('The autocorrelation of packet received')
    plt.xlabel('#(N+k)')
    plt.ylabel('The probability that #(N+k) received')
    plt.show()
    plt.savefig(f'./result/{dataname}/packet_received.png')
    # 4.2 假设#N报文成功未收到, 画柱状图统计[N-10, N+10]未收到的概率(0-1之间)
    plt.plot(lost_probs.keys(),
            lost_probs.values(),
            label='The proportion of lost packets (%)')
    plt.title('The autocorrelation of packet lost')
    plt.xlabel('#(N+k)')
    plt.ylabel('The probability that #(N+k) lost')
    plt.show()
    plt.savefig(f'./result/{dataname}/packet_lost.png')

    # 5. 最小的RTT
    min_rtt = 10000
    max_rtt = 0
    for i in range(1, MAX_SEQ):
        if i in ping_data:
            rtt = ping_data[i]['rtt']
            if rtt < min_rtt:
                min_rtt = rtt
            if rtt > max_rtt:
                max_rtt = rtt
    print(f"最小的RTT: {min_rtt} ms")

    # 6. 最大的RTT
    print(f"最大的RTT: {max_rtt} ms")

    # 7. 画折线图, x轴是一天的真实小时, y轴是RTT的值
    base_time = 0
    for i in range(1, MAX_SEQ):
        if i in ping_data:
            base_time = ping_data[i]['timestamp']
            break
    # 将秒数转换为datetime对象（假设起始时间为当前时间）
    base_time = datetime.now()
    timestamps = []
    rtt_ms = []
    for i in range(1, MAX_SEQ):
        if i not in ping_data:
            continue
        timestamps.append(datetime.fromtimestamp(ping_data[i]['timestamp']))
        rtt_ms.append(ping_data[i]['rtt'])

    # fig, ax = plt.subplots(figsize=(10, 8))  # 创建画布和坐标轴
    plt.plot(timestamps, rtt_ms, label='RTT')  # 绘制折线图
    ax.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M:%S'))  # 设置X轴为时间格式显示时分秒
    ax.xaxis.set_major_locator(mdates.MinuteLocator(interval=10))  #每10分钟一个刻度
    plt.xticks(rotation=45)  # 旋转X轴标签避免重叠
    ax.set_ylabel('RTT (ms)')
    ax.set_xlabel('Time (hours)')
    ax.set_title('RTT over Time')
    ax.grid(True, linestyle='--', alpha=0.6)
    plt.show()
    plt.savefig(f'./result/{dataname}/line_chart.png')

    # 8. 画cumulaive distribution function (CDF) 图, x轴是RTT的值, y轴是RTT的概率(0-1之间)
    rtt_cnt = {}
    for i in range(1, MAX_SEQ):
        if i not in ping_data:
            continue
        rtt = ping_data[i]['rtt']
        rtt_cnt[rtt] = rtt_cnt.get(rtt, 0) + 1
    sorted_rtt = dict(sorted(rtt_cnt.items()))
    rtt_ratio = {k: v / len(ping_data) for k, v in sorted_rtt.items()}
    plt.plot(rtt_ratio.keys(), rtt_ratio.values(), label='Empirical CDF')
    plt.xlabel('RTT (ms)')
    plt.ylabel('The proportion of RTT (ms)')
    plt.title('The proportion of different RTT in total time (CDF)')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.show()
    plt.savefig(f'./result/{dataname}/cdf.png')

    # 9. 画散点图, x轴是RTT的值, y轴是下一个RTT的值, 判断相关性
    rtt_values = []
    for i in range(1, MAX_SEQ):
        if i in ping_data:
            rtt_values.append(ping_data[i]['rtt'])
    x = rtt_values[:-1]
    y = rtt_values[1:]
    plt.scatter(x, y, s=10)
    plt.title('RTT Correlation')
    plt.xlabel('RTT (ms)')
    plt.ylabel('The next RTT (ms)')
    plt.show()
    plt.savefig(f'./result/{dataname}/scatter.png')