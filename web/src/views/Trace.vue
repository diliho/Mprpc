<template>
  <div>
    <el-card>
      <template #header>链路追踪</template>
      <el-input v-model="traceId" placeholder="输入 TraceID 查询" style="width: 500px; margin-bottom: 16px">
        <template #append>
          <el-button @click="searchTrace" :loading="searching">查询</el-button>
        </template>
      </el-input>

      <div v-if="traceDetail">
        <el-descriptions :column="3" border size="small">
          <el-descriptions-item label="TraceID">{{ traceDetail.trace_id }}</el-descriptions-item>
          <el-descriptions-item label="总耗时">{{ traceDetail.total_elapsed_ms }}ms</el-descriptions-item>
          <el-descriptions-item label="状态">
            <el-tag :type="traceDetail.status === 'success' ? 'success' : 'danger'" size="small">{{ traceDetail.status }}</el-tag>
          </el-descriptions-item>
        </el-descriptions>

        <div style="margin-top: 20px">
          <h4>调用链瀑布图</h4>
          <div v-for="(span, idx) in traceDetail.spans" :key="idx" class="trace-span">
            <div class="span-label">{{ span.phase }}</div>
            <div class="span-bar-container">
              <div class="span-bar" :style="{ width: barWidth(span.elapsed_ms) + '%', background: phaseColor(span.phase) }">
                {{ span.elapsed_ms }}ms
              </div>
            </div>
          </div>
        </div>
      </div>

      <el-divider />

      <h4>最近追踪记录</h4>
      <el-table :data="traces" size="small" stripe>
        <el-table-column prop="trace_id" label="TraceID" width="180" />
        <el-table-column prop="service" label="服务" />
        <el-table-column prop="method" label="方法" />
        <el-table-column prop="total_elapsed_ms" label="耗时(ms)" width="100" />
        <el-table-column prop="timestamp" label="时间" width="180" />
        <el-table-column label="操作" width="80">
          <template #default="{ row }">
            <el-button size="small" link @click="traceId = row.trace_id; searchTrace()">查看</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import api from '../api/client'

const traceId = ref('')
const searching = ref(false)
const traceDetail = ref<any>(null)
const traces = ref<any[]>([])

async function searchTrace() {
  if (!traceId.value.trim()) return
  searching.value = true
  try {
    const resp = await api.get(`/api/v1/trace/${traceId.value}`)
    traceDetail.value = resp.data
  } catch { ElMessage.warning('未找到该 TraceID'); traceDetail.value = null }
  searching.value = false
}

async function fetchTraces() {
  try { traces.value = (await api.get('/api/v1/trace')).data } catch {}
}

function barWidth(elapsed: number) {
  const max = traceDetail.value?.total_elapsed_ms || 1
  return Math.max(10, (elapsed / max) * 100)
}

function phaseColor(phase: string) {
  const colors: Record<string, string> = {
    connect: '#1890ff',
    send: '#52c41a',
    process: '#faad14',
    recv: '#722ed1',
  }
  return colors[phase] || '#999'
}

onMounted(fetchTraces)
</script>

<style scoped>
.trace-span { display: flex; align-items: center; margin: 8px 0; }
.span-label { width: 120px; font-size: 13px; color: #666; }
.span-bar-container { flex: 1; background: #f5f5f5; border-radius: 4px; height: 28px; }
.span-bar {
  height: 100%;
  border-radius: 4px;
  color: #fff;
  font-size: 12px;
  display: flex;
  align-items: center;
  padding: 0 8px;
  min-width: 40px;
  transition: width 0.3s;
}
</style>
