<template>
  <div>
    <el-row :gutter="20" class="stat-cards">
      <el-col :span="6">
        <el-card shadow="hover">
          <div class="stat-card">
            <div class="stat-icon" style="background: #e6f7ff"><el-icon :size="32" color="#1890ff"><Monitor /></el-icon></div>
            <div><div class="stat-value">{{ overview.total_nodes }}</div><div class="stat-label">总节点数</div></div>
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover">
          <div class="stat-card">
            <div class="stat-icon" style="background: #f6ffed"><el-icon :size="32" color="#52c41a"><CircleCheck /></el-icon></div>
            <div><div class="stat-value">{{ overview.online_nodes }}</div><div class="stat-label">在线节点</div></div>
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover">
          <div class="stat-card">
            <div class="stat-icon" style="background: #fff7e6"><el-icon :size="32" color="#fa8c16"><Connection /></el-icon></div>
            <div><div class="stat-value">{{ overview.total_services }}</div><div class="stat-label">服务总数</div></div>
          </div>
        </el-card>
      </el-col>
      <el-col :span="6">
        <el-card shadow="hover">
          <div class="stat-card">
            <div class="stat-icon" style="background: #fff1f0"><el-icon :size="32" color="#f5222d"><Setting /></el-icon></div>
            <div><div class="stat-value">{{ overview.active_rate_rules }}</div><div class="stat-label">活跃规则</div></div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="20" style="margin-top: 20px">
      <el-col :span="16">
        <el-card>
          <template #header>实时指标 (QPS / 延迟 / 错误率)</template>
          <div ref="chartRef" style="height: 320px"></div>
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card>
          <template #header>节点列表</template>
          <el-table :data="nodes" size="small" max-height="320">
            <el-table-column prop="ip" label="IP" />
            <el-table-column prop="port" label="端口" width="80" />
            <el-table-column prop="status" label="状态" width="90">
              <template #default="{ row }">
                <el-tag :type="row.status === 'online' ? 'success' : 'danger'" size="small">{{ row.status }}</el-tag>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import { Monitor, CircleCheck, Connection, Setting } from '@element-plus/icons-vue'
import * as echarts from 'echarts'
import api from '../api/client'

const overview = ref({ total_nodes: 0, online_nodes: 0, total_services: 0, total_instances: 0, active_rate_rules: 0, active_breaker_rules: 0 })
const nodes = ref<any[]>([])
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null
let timer: number | null = null

async function fetchData() {
  try {
    const [ov, nd] = await Promise.all([
      api.get('/api/v1/cluster/overview'),
      api.get('/api/v1/cluster/nodes'),
    ])
    overview.value = ov.data
    nodes.value = nd.data
  } catch { /* backend offline */ }
}

function initChart() {
  if (!chartRef.value) return
  chart = echarts.init(chartRef.value)
  chart.setOption({
    tooltip: { trigger: 'axis' },
    legend: { data: ['QPS', '延迟(ms)', '错误率(%)'] },
    grid: { left: 50, right: 20, top: 40, bottom: 30 },
    xAxis: { type: 'category', data: [] },
    yAxis: [
      { type: 'value', name: 'QPS' },
      { type: 'value', name: 'ms / %' },
    ],
    series: [
      { name: 'QPS', type: 'line', data: [], smooth: true, areaStyle: { opacity: 0.1 } },
      { name: '延迟(ms)', type: 'line', yAxisIndex: 1, data: [], smooth: true },
      { name: '错误率(%)', type: 'line', yAxisIndex: 1, data: [], smooth: true },
    ],
  })
}

async function refreshChart() {
  try {
    const resp = await api.get('/api/v1/monitor/trend', { params: { minutes: 30 } })
    const d = resp.data
    if (chart && d.timestamps?.length) {
      chart.setOption({
        xAxis: { data: d.timestamps },
        series: [
          { data: d.qps_values },
          { data: d.latency_values },
          { data: d.error_values },
        ],
      })
    }
  } catch { /* backend offline */ }
}

onMounted(() => {
  fetchData()
  initChart()
  refreshChart()
  timer = window.setInterval(() => { fetchData(); refreshChart() }, 15000)
})

onUnmounted(() => { if (timer) clearInterval(timer); chart?.dispose() })
</script>

<style scoped>
.stat-card { display: flex; align-items: center; gap: 16px; }
.stat-icon { width: 64px; height: 64px; border-radius: 12px; display: flex; align-items: center; justify-content: center; }
.stat-value { font-size: 28px; font-weight: 600; }
.stat-label { color: #999; font-size: 13px; }
</style>
