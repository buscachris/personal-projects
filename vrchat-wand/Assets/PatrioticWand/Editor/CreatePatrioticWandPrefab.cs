using System;
using System.Reflection;
using UnityEditor;
using UnityEditor.Animations;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace PatrioticWand.Editor
{
    public static class CreatePatrioticWandPrefab
    {
        private const string RootPath = "Assets/PatrioticWand";
        private const string GeneratedPath = RootPath + "/Generated";

        [MenuItem("Tools/Patriotic Wand/Create Avatar Prop Prefab")]
        public static void CreateAvatarPropPrefab()
        {
            EnsureFolders();
            GameObject root = BuildWand(addWorldPhysics: false, addPickup: false, addLight: true);
            string prefabPath = GeneratedPath + "/Patriotic_Wand_Avatar_Prop.prefab";
            SavePrefab(root, prefabPath);
            Debug.Log("Created " + prefabPath + ". Parent it under an avatar hand bone and drive active state from an Expressions toggle.");
        }

        [MenuItem("Tools/Patriotic Wand/Create Quest Safe Avatar Prop Prefab")]
        public static void CreateQuestSafeAvatarPropPrefab()
        {
            EnsureFolders();
            GameObject root = BuildWand(addWorldPhysics: false, addPickup: false, addLight: false);
            string prefabPath = GeneratedPath + "/Patriotic_Wand_Avatar_Prop_NoLight.prefab";
            SavePrefab(root, prefabPath);
            Debug.Log("Created " + prefabPath + ". This version omits the point light for stricter avatar/mobile checks.");
        }

        [MenuItem("Tools/Patriotic Wand/Create World Pickup Prefab")]
        public static void CreateWorldPickupPrefab()
        {
            EnsureFolders();
            GameObject root = BuildWand(addWorldPhysics: true, addPickup: true, addLight: true);
            string prefabPath = GeneratedPath + "/Patriotic_Wand_World_Pickup.prefab";
            SavePrefab(root, prefabPath);
            Debug.Log("Created " + prefabPath + ". Place it in a VRChat world scene and upload the world.");
        }

        [MenuItem("Tools/Patriotic Wand/Create Test World Scene")]
        public static void CreateTestWorldScene()
        {
            EnsureFolders();

            Scene scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            scene.name = "Patriotic_Wand_Test_World";

            GameObject floor = GameObject.CreatePrimitive(PrimitiveType.Cube);
            floor.name = "Floor";
            floor.transform.localScale = new Vector3(6f, 0.08f, 6f);
            floor.transform.localPosition = new Vector3(0f, -0.04f, 0f);
            floor.GetComponent<Renderer>().sharedMaterial = CreateMaterial("PW_Test_Floor", new Color(0.18f, 0.18f, 0.18f, 1f));

            GameObject lightObject = new GameObject("Directional Light");
            Light light = lightObject.AddComponent<Light>();
            light.type = LightType.Directional;
            light.intensity = 1.1f;
            lightObject.transform.rotation = Quaternion.Euler(50f, -30f, 0f);

            GameObject cameraObject = new GameObject("Preview Camera");
            Camera camera = cameraObject.AddComponent<Camera>();
            camera.transform.position = new Vector3(0f, 1.4f, -3f);
            camera.transform.rotation = Quaternion.Euler(18f, 0f, 0f);

            GameObject wand = BuildWand(addWorldPhysics: true, addPickup: true, addLight: true);
            wand.transform.position = new Vector3(0f, 0.8f, 0f);
            wand.transform.rotation = Quaternion.Euler(0f, 35f, 0f);

            string scenePath = GeneratedPath + "/Patriotic_Wand_Test_World.unity";
            EditorSceneManager.SaveScene(scene, scenePath);
            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();
            Selection.activeObject = AssetDatabase.LoadAssetAtPath<SceneAsset>(scenePath);
            Debug.Log("Created " + scenePath + ". Add a VRCWorld prefab/spawn if this is a real VRChat world project, then build/test with the VRChat SDK.");
        }

        [MenuItem("Tools/Patriotic Wand/Create Wand Prefab")]
        public static void CreatePrefab()
        {
            CreateWorldPickupPrefab();
        }

        private static GameObject BuildWand(bool addWorldPhysics, bool addPickup, bool addLight)
        {
            Material bodyMaterial = CreateMaterial("PW_Body_Red", new Color(0.82f, 0.02f, 0.05f, 1f));
            Material handleMaterial = CreateMaterial("PW_Handle_Blue", new Color(0.02f, 0.08f, 0.62f, 1f));
            Material tipMaterial = CreateMaterial("PW_Tip_White", Color.white);
            Material trailMaterial = CreateTrailMaterial();

            GameObject root = new GameObject("Patriotic_Wand_Item");
            root.transform.position = Vector3.zero;

            GameObject handle = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
            handle.name = "Handle_Blue";
            handle.transform.SetParent(root.transform, false);
            handle.transform.localScale = new Vector3(0.035f, 0.32f, 0.035f);
            handle.transform.localRotation = Quaternion.Euler(90f, 0f, 0f);
            handle.transform.localPosition = new Vector3(0f, 0f, 0.18f);
            handle.GetComponent<Renderer>().sharedMaterial = handleMaterial;

            GameObject shaft = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
            shaft.name = "Shaft_Red";
            shaft.transform.SetParent(root.transform, false);
            shaft.transform.localScale = new Vector3(0.025f, 0.42f, 0.025f);
            shaft.transform.localRotation = Quaternion.Euler(90f, 0f, 0f);
            shaft.transform.localPosition = new Vector3(0f, 0f, -0.12f);
            shaft.GetComponent<Renderer>().sharedMaterial = bodyMaterial;

            GameObject tip = GameObject.CreatePrimitive(PrimitiveType.Sphere);
            tip.name = "Tip_Trail_Emitter";
            tip.transform.SetParent(root.transform, false);
            tip.transform.localScale = Vector3.one * 0.075f;
            tip.transform.localPosition = new Vector3(0f, 0f, -0.58f);
            tip.GetComponent<Renderer>().sharedMaterial = tipMaterial;

            TrailRenderer trail = tip.AddComponent<TrailRenderer>();
            trail.time = 1.6f;
            trail.minVertexDistance = 0.025f;
            trail.widthMultiplier = 0.08f;
            trail.textureMode = LineTextureMode.Stretch;
            trail.alignment = LineAlignment.View;
            trail.material = trailMaterial;
            trail.colorGradient = CreateTrailGradient();

            if (addLight)
            {
                Light light = tip.AddComponent<Light>();
                light.type = LightType.Point;
                light.range = 1.2f;
                light.intensity = 1.2f;
                light.color = Color.white;
                AddLightAnimator(tip);
            }

            if (addWorldPhysics)
            {
                Rigidbody rigidbody = root.AddComponent<Rigidbody>();
                rigidbody.mass = 0.25f;
                SetRigidbodyDamping(rigidbody, 0.2f, 0.4f);

                BoxCollider collider = root.AddComponent<BoxCollider>();
                collider.center = new Vector3(0f, 0f, -0.12f);
                collider.size = new Vector3(0.16f, 0.16f, 0.95f);
            }

            if (addPickup)
            {
                TryAddVRCPickup(root);
            }

            return root;
        }

        private static void SavePrefab(GameObject root, string prefabPath)
        {
            PrefabUtility.SaveAsPrefabAsset(root, prefabPath);
            UnityEngine.Object.DestroyImmediate(root);

            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();
            Selection.activeObject = AssetDatabase.LoadAssetAtPath<GameObject>(prefabPath);
        }

        private static void EnsureFolders()
        {
            if (!AssetDatabase.IsValidFolder(GeneratedPath))
            {
                AssetDatabase.CreateFolder(RootPath, "Generated");
            }
        }

        private static Material CreateMaterial(string name, Color color)
        {
            string path = GeneratedPath + "/" + name + ".mat";
            Material material = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (material == null)
            {
                material = new Material(Shader.Find("Standard"));
                AssetDatabase.CreateAsset(material, path);
            }

            material.color = color;
            return material;
        }

        private static Material CreateTrailMaterial()
        {
            string path = GeneratedPath + "/PW_Trail_Additive.mat";
            Material material = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (material == null)
            {
                Shader shader = Shader.Find("Particles/Standard Unlit");
                material = new Material(shader != null ? shader : Shader.Find("Standard"));
                AssetDatabase.CreateAsset(material, path);
            }

            material.color = Color.white;
            return material;
        }

        private static Gradient CreateTrailGradient()
        {
            Gradient gradient = new Gradient();
            gradient.SetKeys(
                new[]
                {
                    new GradientColorKey(new Color(0.95f, 0.05f, 0.07f, 1f), 0f),
                    new GradientColorKey(Color.white, 0.5f),
                    new GradientColorKey(new Color(0.05f, 0.18f, 0.95f, 1f), 1f)
                },
                new[]
                {
                    new GradientAlphaKey(1f, 0f),
                    new GradientAlphaKey(0.85f, 0.55f),
                    new GradientAlphaKey(0f, 1f)
                });
            return gradient;
        }

        private static void AddLightAnimator(GameObject tip)
        {
            string clipPath = GeneratedPath + "/PW_Tip_Light_Pulse.anim";
            string controllerPath = GeneratedPath + "/PW_Tip_Light.controller";

            AnimationClip clip = AssetDatabase.LoadAssetAtPath<AnimationClip>(clipPath);
            if (clip == null)
            {
                clip = new AnimationClip
                {
                    name = "PW_Tip_Light_Pulse",
                    frameRate = 30f,
                    legacy = false,
                    wrapMode = WrapMode.Loop
                };
                AssetDatabase.CreateAsset(clip, clipPath);
            }

            SetLightColorCurve(clip, "r", 0.95f, 1f, 0.05f, 0.95f);
            SetLightColorCurve(clip, "g", 0.05f, 1f, 0.18f, 0.05f);
            SetLightColorCurve(clip, "b", 0.07f, 1f, 0.95f, 0.07f);
            AnimationUtility.SetEditorCurve(clip, EditorCurveBinding.FloatCurve("", typeof(Light), "m_Intensity"), AnimationCurve.Linear(0f, 0.8f, 1.2f, 1.4f));

            AnimatorController controller = AssetDatabase.LoadAssetAtPath<AnimatorController>(controllerPath);
            if (controller == null)
            {
                controller = AnimatorController.CreateAnimatorControllerAtPath(controllerPath);
                controller.AddMotion(clip);
            }

            Animator animator = tip.AddComponent<Animator>();
            animator.runtimeAnimatorController = controller;
        }

        private static void SetLightColorCurve(AnimationClip clip, string channel, float c0, float c1, float c2, float c3)
        {
            AnimationCurve curve = new AnimationCurve(
                new Keyframe(0f, c0),
                new Keyframe(0.4f, c1),
                new Keyframe(0.8f, c2),
                new Keyframe(1.2f, c3));

            AnimationUtility.SetEditorCurve(clip, EditorCurveBinding.FloatCurve("", typeof(Light), "m_Color." + channel), curve);
        }

        private static void SetRigidbodyDamping(Rigidbody rigidbody, float linear, float angular)
        {
            Type type = typeof(Rigidbody);
            PropertyInfo linearDamping = type.GetProperty("linearDamping", BindingFlags.Instance | BindingFlags.Public);
            PropertyInfo angularDamping = type.GetProperty("angularDamping", BindingFlags.Instance | BindingFlags.Public);
            if (linearDamping != null && angularDamping != null)
            {
                linearDamping.SetValue(rigidbody, linear, null);
                angularDamping.SetValue(rigidbody, angular, null);
                return;
            }

            PropertyInfo drag = type.GetProperty("drag", BindingFlags.Instance | BindingFlags.Public);
            PropertyInfo angularDrag = type.GetProperty("angularDrag", BindingFlags.Instance | BindingFlags.Public);
            if (drag != null)
            {
                drag.SetValue(rigidbody, linear, null);
            }

            if (angularDrag != null)
            {
                angularDrag.SetValue(rigidbody, angular, null);
            }
        }

        private static void TryAddVRCPickup(GameObject root)
        {
            Type pickupType = FindType("VRC.SDK3.Components.VRCPickup");
            if (pickupType == null)
            {
                Debug.Log("VRChat SDK3 was not found, so the prefab was created without VRCPickup. Add VRCPickup manually after importing into a VRChat world project.");
                return;
            }

            Component pickup = root.AddComponent(pickupType);
            SetFieldOrProperty(pickup, "pickupable", true);
            SetFieldOrProperty(pickup, "proximity", 0.35f);
        }

        private static Type FindType(string fullName)
        {
            foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type type = assembly.GetType(fullName);
                if (type != null)
                {
                    return type;
                }
            }

            return null;
        }

        private static void SetFieldOrProperty(Component component, string name, object value)
        {
            Type type = component.GetType();
            FieldInfo field = type.GetField(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (field != null)
            {
                field.SetValue(component, value);
                return;
            }

            PropertyInfo property = type.GetProperty(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (property != null && property.CanWrite)
            {
                property.SetValue(component, value, null);
            }
        }
    }
}
