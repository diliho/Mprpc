<template>
  <div>
    <el-tabs v-model="activeTab">
      <el-tab-pane label="限流规则" name="ratelimit">
        <el-card>
          <template #header>
            <div style="display: flex; justify-content: space-between; align-items: center">
              <span>限流规则</span>
              <el-button type="primary" size="small" @click="showRateDialog = true"><el-icon><Plus /></el-icon> 新增</el-button>
            </div>
          </template>
          <el-table :data="rateRules" stripe>
            <el-table-column prop="service_name" label="服务" />
            <el-table-column prop="method_name" label="方法" />
            <el-table-column prop="max_qps" label="最大QPS" width="100" />
            <el-table-column prop="algorithm" label="算法" width="120" />
            <el-table-column prop="enabled" label="状态" width="80">
              <template #default="{ row }">
                <el-switch v-model="row.enabled" @change="toggleRate(row)" />
              </template>
            </el-table-column>
            <el-table-column label="操作" width="100">
              <template #default="{ row }">
                <el-popconfirm title="确认删除?" @confirm="deleteRate(row.id)">
                  <template #reference><el-button size="small" type="danger">删除</el-button></template>
                </el-popconfirm>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-tab-pane>

      <el-tab-pane label="熔断规则" name="breaker">
        <el-card>
          <template #header>
            <div style="display: flex; justify-content: space-between; align-items: center">
              <span>熔断规则</span>
              <el-button type="primary" size="small" @click="showBreakerDialog = true"><el-icon><Plus /></el-icon> 新增</el-button>
            </div>
          </template>
          <el-table :data="breakerRules" stripe>
            <el-table-column prop="service_name" label="服务" />
            <el-table-column prop="method_name" label="方法" />
            <el-table-column prop="failure_threshold" label="失败阈值" width="100" />
            <el-table-column prop="timeout_sec" label="恢复超时(s)" width="110" />
            <el-table-column prop="enabled" label="状态" width="80">
              <template #default="{ row }">
                <el-switch v-model="row.enabled" @change="toggleBreaker(row)" />
              </template>
            </el-table-column>
            <el-table-column label="操作" width="100">
              <template #default="{ row }">
                <el-popconfirm title="确认删除?" @confirm="deleteBreaker(row.id)">
                  <template #reference><el-button size="small" type="danger">删除</el-button></template>
                </el-popconfirm>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-tab-pane>
    </el-tabs>

    <el-dialog v-model="showRateDialog" title="新增限流规则" width="440px">
      <el-form :model="rateForm" label-width="100px">
        <el-form-item label="服务名"><el-input v-model="rateForm.service_name" placeholder="* 表示全部" /></el-form-item>
        <el-form-item label="方法名"><el-input v-model="rateForm.method_name" placeholder="* 表示全部" /></el-form-item>
        <el-form-item label="最大QPS"><el-input-number v-model="rateForm.max_qps" :min="1" /></el-form-item>
        <el-form-item label="算法">
          <el-select v-model="rateForm.algorithm">
            <el-option label="令牌桶" value="token_bucket" />
            <el-option label="滑动窗口" value="sliding_window" />
            <el-option label="漏桶" value="leaky_bucket" />
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showRateDialog = false">取消</el-button>
        <el-button type="primary" @click="addRate">确定</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="showBreakerDialog" title="新增熔断规则" width="440px">
      <el-form :model="breakerForm" label-width="100px">
        <el-form-item label="服务名"><el-input v-model="breakerForm.service_name" placeholder="* 表示全部" /></el-form-item>
        <el-form-item label="方法名"><el-input v-model="breakerForm.method_name" placeholder="* 表示全部" /></el-form-item>
        <el-form-item label="失败阈值"><el-input-number v-model="breakerForm.failure_threshold" :min="1" /></el-form-item>
        <el-form-item label="恢复超时(s)"><el-input-number v-model="breakerForm.timeout_sec" :min="5" /></el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showBreakerDialog = false">取消</el-button>
        <el-button type="primary" @click="addBreaker">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Plus } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import api from '../api/client'

const activeTab = ref('ratelimit')
const rateRules = ref<any[]>([])
const breakerRules = ref<any[]>([])
const showRateDialog = ref(false)
const showBreakerDialog = ref(false)
const rateForm = ref({ service_name: '*', method_name: '*', max_qps: 1000, algorithm: 'token_bucket', enabled: true })
const breakerForm = ref({ service_name: '*', method_name: '*', failure_threshold: 5, timeout_sec: 30, enabled: true })

async function fetchRateRules() { try { rateRules.value = (await api.get('/api/v1/governance/ratelimit')).data } catch {} }
async function fetchBreakerRules() { try { breakerRules.value = (await api.get('/api/v1/governance/circuitbreaker')).data } catch {} }

async function addRate() {
  await api.post('/api/v1/governance/ratelimit', rateForm.value)
  ElMessage.success('已创建'); showRateDialog.value = false; fetchRateRules()
}
async function deleteRate(id: number) { await api.delete(`/api/v1/governance/ratelimit/${id}`); fetchRateRules() }
async function toggleRate(row: any) { await api.put(`/api/v1/governance/ratelimit/${row.id}`, { enabled: row.enabled }) }

async function addBreaker() {
  await api.post('/api/v1/governance/circuitbreaker', breakerForm.value)
  ElMessage.success('已创建'); showBreakerDialog.value = false; fetchBreakerRules()
}
async function deleteBreaker(id: number) { await api.delete(`/api/v1/governance/circuitbreaker/${id}`); fetchBreakerRules() }
async function toggleBreaker(row: any) { await api.put(`/api/v1/governance/circuitbreaker/${row.id}`, { enabled: row.enabled }) }

onMounted(() => { fetchRateRules(); fetchBreakerRules() })
</script>
