<template>
  <div>
    <el-row :gutter="20">
      <el-col :span="8">
        <el-card shadow="never">
          <div class="metric-card">
            <div class="metric-value" style="color: #1890ff">{{ summary.total_qps }}</div>
            <div class="metric-label">当前 QPS</div>
          </div>
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card shadow="never">
          <div class="metric-card">
            <div class="metric-value" style="color: #faad14">{{ summary.avg_latency_ms }}ms</div>
            <div class="metric-label">平均延迟</div>
          </div>
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card shadow="never">
          <div class="metric-card">
            <div class="metric-value" style="color: #f5222d">{{ summary.avg_error_rate }}%</div>
            <div class="metric-label">错误率</div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="20" style="margin-top: 20px">
      <el-col :span="12">
        <el-card>
          <template #header>QPS 趋势</template>
          <div ref="qpsChart" style="height: 300px"></div>
        </el-card>
      </el-col>
      <el-col :span="12">
        <el-card>
          <template #header>延迟趋势</template>
          <div ref="latChart" style="height: 300px"></div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="20" style="margin-top: 20px">
      <el-col :span="12">
        <el-card>
          <template #header>错误率趋势</template>
          <div ref="errChart" style="height: 300px"></div>
        </el-card>
      </el-col>
      <el-col :span="12">
        <el-card>
          <template #header>Grafana 面板</template>
          <div v-if="grafanaUrl" style="height: 300px">
            <iframe :src="grafanaUrl" width="100%" height="100%" frameborder="0"></iframe>
          </div>
          <el-empty v-else description="Grafana 未连接" />
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import api from '../api/client'

const summary = ref({ total_qps: 0, avg_latency_ms: 0, avg_error_rate: 0, total_calls: 0 })
const grafanaUrl = ref('')
const qpsChart = ref<HTMLElement>()
const latChart = ref<HTMLElement>()
const errChart = ref<HTMLElement>()
let qpsE: echarts.ECharts | null = null
let latE: echarts.ECharts | null = null
let errE: echarts.ECharts | null = null
let timer: number | null = null

function makeLineOption(name: string, color: string) {
  return {
    tooltip: { trigger: 'axis' },
    grid: { left: 50, right: 15, top: 20, bottom: 30 },
    xAxis: { type: 'category', data: [] },
    yAxis: { type: 'value' },
    series: [{ name, type: 'line', data: [], smooth: true, lineStyle: { color }, itemStyle: { color }, areaStyle: { color, opacity: 0.08 } }],
  }
}

async function refresh() {
  try {
    const [sumResp, trendResp] = await Promise.all([
      api.get('/api/v1/monitor/summary'),
      api.get('/api/v1/monitor/trend', { params: { minutes: 30 } }),
    ])
    summary.value = sumResp.data
    const t = trendResp.data
    if (t.timestamps?.length) {
      qpsE?.setOption({ xAxis: { data: t.timestamps }, series: [{ data: t.qps_values }] })
      latE?.setOption({ xAxis: { data: t.timestamps }, series: [{ data: t.latency_values }] })
      errE?.setOption({ xAxis: { data: t.timestamps }, series: [{ data: t.error_values }] })
    }
  } catch { /* backend offline */ }
  try {
    const gr = await api.get('/api/v1/monitor/grafana/embed')
    grafanaUrl.value = gr.data.url
  } catch { grafanaUrl.value = '' }
}

onMounted(() => {
  if (qpsChart.value) { qpsE = echarts.init(qpsChart.value); qpsE.setOption(makeLineOption('QPS', '#1890ff')) }
  if (latChart.value) { latE = echarts.init(latChart.value); latE.setOption(makeLineOption('延迟(ms)', '#faad14')) }
  if (errChart.value) { errE = echarts.init(errChart.value); errE.setOption(makeLineOption('错误率(%)', '#f5222d')) }
  refresh()
  timer = window.setInterval(refresh, 15000)
})

onUnmounted(() => { if (timer) clearInterval(timer); qpsE?.dispose(); latE?.dispose(); errE?.dispose() })
</script>

<style scoped>
.metric-card { text-align: center; padding: 20px 0; }
.metric-value { font-size: 32px; font-weight: 700; }
.metric-label { color: #999; margin-top: 4px; }
</style>
